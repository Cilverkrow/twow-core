// Unit suite for ai::PlayerbotDatabaseContract -- the three SQL builders that
// every runtime write to the PlayerBot event store is supposed to go through.
//
// Ported from twow-repo, where the header and these tests were written
// together. Core received the header on its own, so until this file landed
// EventUpsertSql, EventDeleteSql and EventWriteSerialId had zero callers in
// core and zero tests: three functions nothing could break loudly.
//
// What each assertion is guarding, in order:
//
//   * the builders read the table name from configuration on every call, so
//     .reload config can repoint the event store without a restart. A builder
//     that captured the name once (a static, a constant) would pass a
//     single-table test and silently keep writing to the old table forever.
//   * the upsert is ONE statement: INSERT ... ON DUPLICATE KEY UPDATE. The
//     shape it replaced was DELETE-then-INSERT, which is two statements, two
//     lock acquisitions and a window in which the row does not exist. No
//     DELETE and no REPLACE may reappear in it.
//   * the placeholder count matches the argument list callers PExecute with.
//     Getting this wrong is not a compile error, it is a runtime bind failure
//     or, worse, values landing in the wrong columns.
//   * the delete names owner, bot and event -- all three. A delete that
//     dropped `event` from the predicate would erase every event for a bot.
//   * EventWriteSerialId is a pure function of (owner, bot, event): stable
//     across calls, different for different keys, never zero. That value is
//     the serialisation lane a write is queued on, so an unstable one puts two
//     writes to the same row on different lanes (the deadlock this whole
//     contract exists to remove) and a colliding one needlessly serialises
//     unrelated rows.
//
// Compiled against a stubbed PlayerbotAIConfig -- see t/stubs/playerbot/.

#include "PlayerbotDatabaseContract.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; \
    ++failures; \
} } while (false)

std::size_t Count(std::string const& value, std::string const& needle)
{
    std::size_t count = 0;
    for (std::size_t position = 0;
         (position = value.find(needle, position)) != std::string::npos;
         position += needle.size())
        ++count;
    return count;
}

void SetConfiguredTable(std::string const& table)
{
    sPlayerbotAIConfig.eventStoreTable = table;
}

// The two spellings a deployment realistically uses: the historical default,
// inline in the character database, and a dedicated schema.
char const* const kDefaultTable = "`ai_playerbot_random_bots`";
char const* const kRepointedTable = "`cv_bots`.`ai_playerbot_random_bots`";

void TestConfiguredTargetIsReadFresh()
{
    SetConfiguredTable(kDefaultTable);
    CHECK(ai::PlayerbotDatabaseContract::EventStoreTable() == kDefaultTable);

    // Read fresh, not cached: repointing config must change what the builders
    // emit without anything else being reset.
    SetConfiguredTable(kRepointedTable);
    CHECK(ai::PlayerbotDatabaseContract::EventStoreTable() == kRepointedTable);
    CHECK(ai::PlayerbotDatabaseContract::EventUpsertSql(false)
        .find(kRepointedTable) != std::string::npos);
    CHECK(ai::PlayerbotDatabaseContract::EventDeleteSql()
        .find(kRepointedTable) != std::string::npos);

    SetConfiguredTable(kDefaultTable);
}

void TestAtomicUpsert()
{
    for (char const* table : {kDefaultTable, kRepointedTable})
    {
        SetConfiguredTable(table);
        for (bool includeData : {false, true})
        {
            std::string const sql = ai::PlayerbotDatabaseContract::EventUpsertSql(includeData);
            CHECK(Count(sql, "INSERT INTO") == 1);
            CHECK(Count(sql, "ON DUPLICATE KEY UPDATE") == 1);
            CHECK(sql.find("DELETE") == std::string::npos);
            CHECK(sql.find("REPLACE") == std::string::npos);
            CHECK(sql.find(table) != std::string::npos);
            CHECK(Count(sql, "?") == (includeData ? 6u : 5u));
            CHECK(sql.find(includeData ? "`data`=VALUES(`data`)" : "`data`=NULL") != std::string::npos);
        }
    }
    SetConfiguredTable(kDefaultTable);
}

void TestPreciseDelete()
{
    for (char const* table : {kDefaultTable, kRepointedTable})
    {
        SetConfiguredTable(table);
        std::string const sql = ai::PlayerbotDatabaseContract::EventDeleteSql();
        CHECK(sql == "DELETE FROM " + std::string(table) +
            " WHERE `owner`=? AND `bot`=? AND `event`=?");
        CHECK(Count(sql, "?") == 3);
    }
    SetConfiguredTable(kDefaultTable);
}

void TestStableSerializationLane()
{
    std::uint32_t const first = ai::PlayerbotDatabaseContract::EventWriteSerialId(0, 42, "add");
    CHECK(first != 0);
    CHECK(first == ai::PlayerbotDatabaseContract::EventWriteSerialId(0, 42, "add"));
    CHECK(first != ai::PlayerbotDatabaseContract::EventWriteSerialId(0, 43, "add"));
    CHECK(first != ai::PlayerbotDatabaseContract::EventWriteSerialId(0, 42, "logout"));
    CHECK(first != ai::PlayerbotDatabaseContract::EventWriteSerialId(1, 42, "add"));

    // The lane id is also independent of the configured table: repointing the
    // event store must not reshuffle which writes serialise against which.
    SetConfiguredTable(kRepointedTable);
    CHECK(first == ai::PlayerbotDatabaseContract::EventWriteSerialId(0, 42, "add"));
    SetConfiguredTable(kDefaultTable);
}
}

int main()
{
    TestConfiguredTargetIsReadFresh();
    TestAtomicUpsert();
    TestPreciseDelete();
    TestStableSerializationLane();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "PLAYERBOT_EVENT_STORE_CONTRACT_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
