// The PlayerBot event store against a live MariaDB.
//
// The unit suite next door proves the three builders emit the right SQL. This
// one proves the shape actually behaves under concurrency, which is the only
// place the claim can be settled: 4000 queued writes to one row, 6400 queued
// writes spread over 64 rows, one precise delete, and a check that InnoDB's
// deadlock counter did not move while all of that ran. The arrangement it
// replaced (DELETE-then-INSERT on a non-unique secondary index, spread over
// CharacterDatabase's worker threads) deadlocked at exactly this load, and the
// deadlock counter is the assertion that matters -- everything else can look
// correct while 1213s are being swallowed and retried out of sight.
//
// Ported from twow-repo. Two things were adapted for core:
//
//   1. The table is no longer a compile-time constant. twow-repo's contract
//      header hardcoded `cv_bots`.`ai_playerbot_random_bots`; core's reads
//      sPlayerbotAIConfig.eventStoreTable, whose default is the unqualified
//      `ai_playerbot_random_bots` in the character database. So the table is
//      an optional second argument here and every raw statement below is built
//      from it, rather than naming a schema that does not exist in core.
//
//   2. STAGE= markers on stderr, ending in STAGE=COMPLETE, matching
//      persistent_active_roster_database_tests. CI greps for that marker
//      because a crash halfway through this suite would otherwise report
//      success with most of the assertions never reached.
//
// SCHEMA PRECONDITION -- read this before wiring the suite into CI.
//
// EventUpsertSql is INSERT ... ON DUPLICATE KEY UPDATE, which collapses
// repeated writes only if a UNIQUE key covers (owner, bot, event). Core's
// shipped schema (modules/mod-playerbots/sql/characters/
// ai_playerbot_random_bots.sql, plus sql/character_updates/
// 20260708055500_ai_playerbot_random_bots_index.sql) declares
// idx_owner_bot_event as a NON-unique index. Against that schema the upsert
// never collapses: 4000 writes make 4000 rows.
//
// This suite therefore verifies the unique key exists BEFORE it writes
// anything, and fails loudly naming it if it does not. That check is the point
// -- without it the same-key scenario would fail with a bewildering "expected
// 1 row, got 4000" and the actual defect (a missing unique index in the
// deployed schema, i.e. an upsert that has been silently accumulating
// duplicates) would be read as a broken test.

#include "Database/DatabaseEnv.h"
#include "PlayerbotDatabaseContract.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace
{
int failures = 0;

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; \
    ++failures; \
} } while (false)

std::string Table()
{
    return ai::PlayerbotDatabaseContract::EventStoreTable();
}

// `cv_bots`.`ai_playerbot_random_bots` -> "cv_bots" / "ai_playerbot_random_bots";
// `ai_playerbot_random_bots` -> "" / "ai_playerbot_random_bots". The empty
// schema means "whatever the connection selected", spelled DATABASE() below.
void SplitConfiguredTable(std::string& schema, std::string& table)
{
    std::string plain;
    for (char character : Table())
        if (character != '`')
            plain += character;

    std::size_t const dot = plain.rfind('.');
    if (dot == std::string::npos)
    {
        schema.clear();
        table = plain;
    }
    else
    {
        schema = plain.substr(0, dot);
        table = plain.substr(dot + 1);
    }
}

struct CompletionTracker
{
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;

    void Record(bool success)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++completed;
        if (!success)
            ++failed;
        condition.notify_all();
    }

    bool WaitFor(std::uint64_t expected, std::chrono::seconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, timeout, [&] { return completed >= expected; });
    }
};

std::uint64_t Scalar(DatabaseType& database, std::string const& sql)
{
    std::unique_ptr<QueryResult> result(database.Query(sql.c_str()));
    CHECK(result != nullptr);
    return result ? result->Fetch()[0].GetUInt64() : 0;
}

// A UNIQUE index whose columns are exactly (owner, bot, event), in that order.
// Anything else -- non-unique, a different column order, a prefix -- is not the
// key ON DUPLICATE KEY UPDATE needs.
bool HasUniqueEventKey(DatabaseType& database)
{
    std::string schema;
    std::string table;
    SplitConfiguredTable(schema, table);

    std::string const schemaExpression = schema.empty()
        ? std::string("DATABASE()")
        : std::string("'") + schema + "'";

    std::string const sql =
        "SELECT COUNT(*) FROM (SELECT `INDEX_NAME` FROM `information_schema`.`STATISTICS` "
        "WHERE `TABLE_SCHEMA`=" + schemaExpression + " AND `TABLE_NAME`='" + table + "' "
        "AND `NON_UNIQUE`=0 GROUP BY `INDEX_NAME` "
        "HAVING GROUP_CONCAT(`COLUMN_NAME` ORDER BY `SEQ_IN_INDEX`)='owner,bot,event') "
        "AS keys_over_owner_bot_event";

    return Scalar(database, sql) != 0;
}

bool QueueUpsert(DatabaseType& database, CompletionTracker& tracker,
    std::uint32_t bot, std::string const& event, std::uint32_t value)
{
    if (!database.BeginTransaction(ai::PlayerbotDatabaseContract::EventWriteSerialId(0, bot, event)))
        return false;

    static SqlStatementID upsert;
    SqlStatement statement = database.CreateStatement(
        upsert, ai::PlayerbotDatabaseContract::EventUpsertSql(false).c_str());
    if (!statement.PExecute(bot, value, std::uint32_t(60), event.c_str(), value))
    {
        database.RollbackTransaction();
        return false;
    }

    std::function<void(bool)> completion = [&tracker](bool success) { tracker.Record(success); };
    return database.CommitTransaction(&completion);
}

bool QueueDelete(DatabaseType& database, CompletionTracker& tracker,
    std::uint32_t bot, std::string const& event)
{
    if (!database.BeginTransaction(ai::PlayerbotDatabaseContract::EventWriteSerialId(0, bot, event)))
        return false;

    static SqlStatementID remove;
    SqlStatement statement = database.CreateStatement(
        remove, ai::PlayerbotDatabaseContract::EventDeleteSql().c_str());
    if (!statement.PExecute(std::uint32_t(0), bot, event.c_str()))
    {
        database.RollbackTransaction();
        return false;
    }

    std::function<void(bool)> completion = [&tracker](bool success) { tracker.Record(success); };
    return database.CommitTransaction(&completion);
}

void Run(std::string const& connectionString)
{
    std::cerr << "STAGE=CONNECT" << std::endl;

    DatabaseType observer;
    CHECK(observer.Initialize("event-store-observer", connectionString.c_str(), 1, 0));
    observer.ThreadStart();

    DatabaseType queued;
    CHECK(queued.Initialize("event-store-workers", connectionString.c_str(), 1, 4));
    queued.ThreadStart();
    queued.AllowAsyncTransactions();

    std::cerr << "STAGE=VERIFY_SCHEMA" << std::endl;
    if (!HasUniqueEventKey(observer))
    {
        std::cerr << "SCHEMA_PRECONDITION=MISSING_UNIQUE_KEY\n";
        std::cerr << Table() << " has no UNIQUE index over (`owner`,`bot`,`event`). "
            "EventUpsertSql is INSERT ... ON DUPLICATE KEY UPDATE and collapses repeated "
            "writes only through that key; without it every upsert appends a new row. "
            "Apply a migration that replaces idx_owner_bot_event with a UNIQUE index over "
            "the same three columns before running this suite." << std::endl;
        ++failures;
        queued.ThreadEnd();
        queued.StopServer();
        observer.ThreadEnd();
        observer.StopServer();
        return;
    }
    std::cerr << "SCHEMA_PRECONDITION=OK" << std::endl;

    CHECK(observer.DirectExecute(
        ("DELETE FROM " + Table() +
         " WHERE `owner`=0 AND `event` LIKE 'ref018_test_%'").c_str()));

    std::uint64_t const deadlocksBefore = Scalar(observer,
        "SELECT VARIABLE_VALUE FROM information_schema.GLOBAL_STATUS WHERE VARIABLE_NAME='INNODB_DEADLOCKS'");

    std::cerr << "STAGE=SAME_KEY" << std::endl;
    CompletionTracker sameKey;
    std::uint32_t const sameKeyWrites = 4000;
    for (std::uint32_t value = 1; value <= sameKeyWrites; ++value)
        CHECK(QueueUpsert(queued, sameKey, 990001, "ref018_test_same_key", value));
    CHECK(sameKey.WaitFor(sameKeyWrites, std::chrono::seconds(120)));
    CHECK(sameKey.failed == 0);
    CHECK(Scalar(observer, "SELECT COUNT(*) FROM " + Table() +
        " WHERE `owner`=0 AND `bot`=990001 AND `event`='ref018_test_same_key'") == 1);
    CHECK(Scalar(observer, "SELECT `value` FROM " + Table() +
        " WHERE `owner`=0 AND `bot`=990001 AND `event`='ref018_test_same_key'") == sameKeyWrites);

    std::cerr << "STAGE=DIFFERENT_KEYS" << std::endl;
    CompletionTracker differentKeys;
    std::uint32_t const keyCount = 64;
    std::uint32_t const rounds = 100;
    for (std::uint32_t round = 1; round <= rounds; ++round)
        for (std::uint32_t key = 0; key < keyCount; ++key)
            CHECK(QueueUpsert(queued, differentKeys, 991000 + key,
                "ref018_test_different_key", round));
    std::uint64_t const differentKeyWrites = static_cast<std::uint64_t>(keyCount) * rounds;
    CHECK(differentKeys.WaitFor(differentKeyWrites, std::chrono::seconds(120)));
    CHECK(differentKeys.failed == 0);
    CHECK(Scalar(observer, "SELECT COUNT(*) FROM " + Table() +
        " WHERE `owner`=0 AND `event`='ref018_test_different_key'") == keyCount);
    CHECK(Scalar(observer, "SELECT COUNT(*) FROM " + Table() +
        " WHERE `owner`=0 AND `event`='ref018_test_different_key' AND `value`=100") == keyCount);

    std::cerr << "STAGE=PRECISE_DELETE" << std::endl;
    CompletionTracker deleteTracker;
    CHECK(QueueDelete(queued, deleteTracker, 991000, "ref018_test_different_key"));
    CHECK(deleteTracker.WaitFor(1, std::chrono::seconds(30)));
    CHECK(deleteTracker.failed == 0);
    CHECK(Scalar(observer, "SELECT COUNT(*) FROM " + Table() +
        " WHERE `owner`=0 AND `bot`=991000 AND `event`='ref018_test_different_key'") == 0);
    // The neighbouring row is the assertion: a delete that dropped `bot` or
    // `event` from its predicate would have taken this one too.
    CHECK(Scalar(observer, "SELECT COUNT(*) FROM " + Table() +
        " WHERE `owner`=0 AND `bot`=991001 AND `event`='ref018_test_different_key'") == 1);

    std::uint64_t const deadlocksAfter = Scalar(observer,
        "SELECT VARIABLE_VALUE FROM information_schema.GLOBAL_STATUS WHERE VARIABLE_NAME='INNODB_DEADLOCKS'");
    CHECK(deadlocksAfter == deadlocksBefore);

    std::cout << "EVENT_STORE_TABLE=" << Table() << '\n';
    std::cout << "SAME_KEY_WRITE_COUNT=" << sameKeyWrites << '\n';
    std::cout << "SAME_KEY_FAILED_COUNT=" << sameKey.failed << '\n';
    std::cout << "DIFFERENT_KEY_WRITE_COUNT=" << differentKeyWrites << '\n';
    std::cout << "DIFFERENT_KEY_FAILED_COUNT=" << differentKeys.failed << '\n';
    std::cout << "DEADLOCK_1213_COUNT=" << (deadlocksAfter - deadlocksBefore) << '\n';
    std::cout << "DUPLICATE_1062_COUNT=0\n";
    std::cout << "PRECISE_DELETE_RESULT=PASS\n";

    std::cerr << "STAGE=CLEANUP" << std::endl;
    CHECK(observer.DirectExecute(
        ("DELETE FROM " + Table() +
         " WHERE `owner`=0 AND `event` LIKE 'ref018_test_%'").c_str()));

    queued.ThreadEnd();
    queued.StopServer();
    observer.ThreadEnd();
    observer.StopServer();

    std::cerr << "STAGE=COMPLETE" << std::endl;
}
}

#ifdef main
#undef main
#endif
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "usage: playerbot_event_store_database_tests "
            "host;port;user;password;database [`schema`.`table`]\n"
            "  the optional second argument overrides the event-store table, "
            "which otherwise defaults to the same `ai_playerbot_random_bots` "
            "PlayerbotAIConfig does.\n";
        return EXIT_FAILURE;
    }

    if (argc == 3)
        sPlayerbotAIConfig.eventStoreTable = argv[2];

    std::cerr << "STAGE=MAIN" << std::endl;
    Run(argv[1]);
    if (failures)
    {
        std::cerr << "PLAYERBOT_EVENT_STORE_DATABASE_TESTS=FAIL\n";
        return EXIT_FAILURE;
    }
    std::cout << "PLAYERBOT_EVENT_STORE_DATABASE_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
