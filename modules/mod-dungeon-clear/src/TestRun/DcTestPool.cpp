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

        // No current version (NULL) or an empty roster: no rows, nothing to exclude.
        std::unique_ptr<QueryResult> members(CharacterDatabase.Query(
            "SELECT m.character_guid FROM ai_playerbot_roster_member m "
            "JOIN ai_playerbot_roster_current c ON c.version_id = m.version_id "
            "WHERE c.singleton_id = 1"));
        if (!members)
            return true;
        do
        {
            guids.insert(members->Fetch()[0].GetUInt32());
        } while (members->NextRow());
        return true;
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
