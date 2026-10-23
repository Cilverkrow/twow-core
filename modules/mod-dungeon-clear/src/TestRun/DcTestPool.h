#pragma once

// twow-repo#629 (DUN-03): the database side of the claim pool rules in
// DcTestPoolPolicy.h. Read once per run setup (a control path on the world
// thread, like the player-cache walk in claim()), never per tick.

#include "TestRun/DcTestPoolPolicy.h"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace DcTestPool
{
    // DungeonClear.TestRun.AccountPrefix ("" = no extra filter).
    std::string ConfAccountPrefix();

    // The current persistent roster's character guids, read from
    // ai_playerbot_roster_member joined with ai_playerbot_roster_current,
    // whatever PersistentActiveRoster.Enabled says. false (with err) when the
    // tables exist but cannot be read, or only one of them exists.
    bool LoadRosterGuids(std::unordered_set<std::uint32_t>& guids, std::string& err);

    // Ids of the accounts whose name starts with `prefix` (case-insensitive).
    // false (with err) for a prefix that is not PrefixIsSafe.
    bool LoadPoolAccounts(std::string const& prefix, std::unordered_set<std::uint32_t>& accounts, std::string& err);
}
