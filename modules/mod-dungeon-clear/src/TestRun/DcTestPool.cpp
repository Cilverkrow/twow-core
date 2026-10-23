/*
 * twow-repo#629 (DUN-03): claim-pool loaders for `.dc test`. See DcTestPool.h.
 */

#include "TestRun/DcTestPool.h"

#include <memory>

#include "Config.h"
#include "DatabaseEnv.h"

namespace DcTestPool
{
    std::string ConfAccountPrefix()
    {
        // String-valued, so the numeric DcSettings registry cannot hold it; read
        // once per run setup with showLogs=false (tools/check_config_reads.py
        // allowlist, like the driver identity keys).
        return sConfigMgr->GetOption<std::string>("DungeonClear.TestRun.AccountPrefix", "", false);
    }

    bool LoadRosterGuids(std::unordered_set<std::uint32_t>& guids, std::string& err)
    {
        guids.clear();

        std::unique_ptr<QueryResult> tables(CharacterDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name IN ('ai_playerbot_roster_member', 'ai_playerbot_roster_current')"));
        if (!tables)
        {
            err = "roster table check failed";
            return false;
        }
        switch (ClassifyRosterTables(tables->Fetch()[0].GetUInt32()))
        {
            case RosterTables::None:
                return true;  // no persistent roster on this realm
            case RosterTables::Partial:
                err = "only one of ai_playerbot_roster_member / ai_playerbot_roster_current exists";
                return false;
            case RosterTables::Both:
                break;
        }

        // A failed query and an empty roster both return no result set, so count first (one row always,
        // 0 with no current version or an empty roster) and hold the guid read against that count.
        std::unique_ptr<QueryResult> count(CharacterDatabase.Query(
            "SELECT COUNT(*) FROM ai_playerbot_roster_member m "
            "JOIN ai_playerbot_roster_current c ON c.version_id = m.version_id "
            "WHERE c.singleton_id = 1"));
        std::uint64_t const counted = count ? count->Fetch()[0].GetUInt64() : 0;
        std::unique_ptr<QueryResult> members;
        if (count && counted)
        {
            members.reset(CharacterDatabase.Query(
                "SELECT m.character_guid FROM ai_playerbot_roster_member m "
                "JOIN ai_playerbot_roster_current c ON c.version_id = m.version_id "
                "WHERE c.singleton_id = 1"));
            if (members)
            {
                do
                {
                    guids.insert(members->Fetch()[0].GetUInt32());
                } while (members->NextRow());
            }
        }
        switch (ClassifyRosterRead(count != nullptr, counted, members != nullptr, guids.size()))
        {
            case RosterRead::Empty:
            case RosterRead::Ok:
                return true;
            case RosterRead::Refuse:
                break;
        }
        err = count ? "roster member read failed or short (" + std::to_string(guids.size()) + " of " +
                          std::to_string(counted) + ")"
                    : "roster member count failed";
        guids.clear();
        return false;
    }

    bool LoadPoolAccounts(std::string const& prefix, std::unordered_set<std::uint32_t>& accounts, std::string& err)
    {
        accounts.clear();
        if (!PrefixIsSafe(prefix))
        {
            err = "DungeonClear.TestRun.AccountPrefix must be letters and digits only (at most 32)";
            return false;
        }

        std::unique_ptr<QueryResult> rows(LoginDatabase.PQuery(
            "SELECT id, username FROM account WHERE username LIKE '%s%%'", prefix.c_str()));
        if (!rows)
            return true;  // no such account: the pool is empty, claim() says so by role
        do
        {
            Field* fields = rows->Fetch();
            // LIKE is collation-dependent; the prefix rule itself decides.
            if (PrefixMatches(fields[1].GetCppString(), prefix))
                accounts.insert(fields[0].GetUInt32());
        } while (rows->NextRow());
        return true;
    }
}
