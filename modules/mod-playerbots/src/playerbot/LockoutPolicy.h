#pragma once

#include <cstdint>
#include <string>

// L1 (owner 07.11.2026): bots reset their raid lockouts only on a real player's invite
// (AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly, code default 1, 0 = rollback). A bot invited by another bot (or by a
// world-thread group manager) keeps its PERM raid binds; the split guard below keeps such a bot out of another copy of the
// same raid. Pure rules, no game header (t/lockout_policy_tests.cpp); the game side is LockoutGuard.h/.cpp and the
// contract t/contracts.d/l1_lockout_source_contract_tests.cmake.
namespace ai::lockout
{
// One bind of a player or a group on one map. instanceId 0 = no bind.
struct Bind
{
    std::uint32_t instanceId = 0;
    bool perm = false;
};

// Model of Player::GetBoundInstanceSaveForSelfOrGroup (src/game/Objects/Player.cpp, guard `if (!pBind || !pBind->perm)`):
// the player's own PERM bind first, then the group bind, then the player's own (solo) bind. 0 = no save, the core mints a
// new instance id (MapManager::CreateInstance).
inline std::uint32_t CoreChoice(Bind self, Bind group)
{
    if (self.instanceId && self.perm)
        return self.instanceId;
    if (group.instanceId)
        return group.instanceId;
    return self.instanceId;
}

// Model of the perm-mismatch assert (DungeonMap::BindPlayerOrGroupOnEnter, src/game/Maps/Map.cpp, MANGOS_ASSERT(false)):
// a player PERM-bound to one copy is put into another. entered 0 = a newly minted id, never equal to a bound one.
inline bool PermAssertWouldFire(Bind self, std::uint32_t entered)
{
    return self.instanceId && self.perm && self.instanceId != entered;
}

// I1 split guard: a bot holding PERM bind selfOnM on map M must not join or follow toward copy targetOnM of M when that is
// another copy (the core would send it into its own copy: a split group, not a crash). targetOnM 0 = no copy known.
inline bool LockSplit(Bind selfOnM, std::uint32_t targetOnM)
{
    return selfOnM.instanceId && selfOnM.perm && targetOnM && targetOnM != selfOnM.instanceId;
}

// L1 (a): whether the accept tail runs "reset raids". Switch off = the old behaviour (always). Switch on = only for a real
// inviter on the direct accept path; the world-thread manager sources and the posted tail are bot-only.
inline bool ResetRaidsOnJoin(bool switchOn, bool worldManagerSource, bool joinTailSource, bool inviterReal)
{
    return !switchOn || (!worldManagerSource && !joinTailSource && inviterReal);
}

// The switch value as written in the config: only "0" (surrounding blanks ignored) turns it off, anything else is on
// (owner default-1 rule: unknown = 1; Config::GetIntDefault is atoi, which would read "on" as 0).
inline bool SwitchOn(std::string const& raw)
{
    std::size_t const first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return true;
    std::size_t const last = raw.find_last_not_of(" \t\r\n");
    return raw.compare(first, last - first + 1, "0") != 0;
}

// The increments of one minute for the [Lockout] line.
struct Counts
{
    std::uint32_t resetReal = 0;      // reset raids ran, inviter a real player
    std::uint32_t resetBot = 0;       // reset raids ran, inviter a bot or a world manager (switch 0 only)
    std::uint32_t keepBot = 0;        // binds kept: switch on, inviter a bot or a world manager
    std::uint32_t splitDecline = 0;   // S1: invite declined (perm bind on the inviter's map, another copy)
    std::uint32_t splitEntry = 0;     // S3: module move into another copy of a perm-bound map refused

    bool Any() const { return resetReal || resetBot || keepBot || splitDecline || splitEntry; }
};

// The minute line, field order fixed (the contract pins it). Printed only in a minute with a non-zero field.
inline std::string MinuteLine(bool switchOn, Counts const& c)
{
    return "[Lockout] minute mode=" + std::string(switchOn ? "1" : "0") +
        " reset_real=" + std::to_string(c.resetReal) +
        " reset_bot=" + std::to_string(c.resetBot) +
        " keep_bot=" + std::to_string(c.keepBot) +
        " split_decline=" + std::to_string(c.splitDecline) +
        " split_entry=" + std::to_string(c.splitEntry);
}
}
