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
// inviter. The bot-only world-manager sources (ad-hoc, quest groups) are bot invites; joinTailSource marks a tail that is
// bot-only by construction (the accept tail passes false since A3 lever 2: its posted tail can carry a real player's invite).
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

// leader_bind_carry (OB-00 07.11.2026): count only, no rule (the rule comes with the raid ADR, O7), at both switch values.
// A group leader holding its own PERM bind on raid map M makes the core copy that bind into the group (Group::Create,
// Group::_setLeader). A member without its own PERM bind on M who enters M is then sent into that copy (the core takes the
// group bind only when the player has no own PERM bind: CoreChoice) and PERM-bound there on entry (the group-perm branch of
// DungeonMap::BindPlayerOrGroupOnEnter). One entry of a member into M counts when all of this holds:
//   - the member had no own PERM bind on M at its far teleport and holds one on the entered copy after the entry
//     (EntryPermFromGroup; a member's own pre-existing PERM bind for the copy is therefore never counted);
//   - the member is not the group leader and the leader is a bot;
//   - the leader's own bind and the group's bind on M are both PERM binds on the entered copy.
// The origin of the group's PERM bind is not an input: a bind the group earned in the current run (a boss kill with the leader
// inside, DungeonMap::PermBindAllPlayers) looks the same and counts too (review M1, documented in conf.dist).
struct CarryEntry
{
    std::uint32_t entered = 0;    // instance id of the copy of M the member entered
    Bind selfBefore;              // the member's own bind on M at its far teleport, before the core binds it on entry
    Bind selfAfter;               // the member's own bind on M after the entry
    bool memberIsLeader = false;
    bool leaderIsBot = false;     // false also for a leader that is not online (not classified, not counted)
    Bind leaderOnM;               // the leader's own bind on M
    Bind groupOnM;                // the group's bind on M
};

// A PERM bind on copy (copy 0 = none).
inline bool PermOn(Bind b, std::uint32_t copy)
{
    return copy && b.perm && b.instanceId == copy;
}

// The member's PERM bind on the entered copy is new at this entry: no own PERM bind on M before, one on the entered copy after.
inline bool EntryPermFromGroup(Bind selfBefore, Bind selfAfter, std::uint32_t entered)
{
    return PermOn(selfAfter, entered) && !(selfBefore.instanceId && selfBefore.perm);
}

inline bool LeaderBindCarry(CarryEntry const& e)
{
    return !e.memberIsLeader && e.leaderIsBot && EntryPermFromGroup(e.selfBefore, e.selfAfter, e.entered) &&
        PermOn(e.groupOnM, e.entered) && PermOn(e.leaderOnM, e.entered);
}

// The increments of one minute for the [Lockout] line.
struct Counts
{
    std::uint32_t resetReal = 0;      // reset raids ran, inviter a real player
    std::uint32_t resetBot = 0;       // reset raids ran, inviter a bot or a world manager (switch 0 only)
    std::uint32_t keepBot = 0;        // binds kept: switch on, inviter a bot or a world manager
    std::uint32_t splitDecline = 0;   // S1: invite declined (perm bind on the inviter's map, another copy)
    std::uint32_t splitEntry = 0;     // S3: module move into another copy of a perm-bound map refused
    std::uint32_t leaderCarryReal = 0;   // leader_bind_carry, the entering member a real player
    std::uint32_t leaderCarryBot = 0;    // leader_bind_carry, the entering member a bot

    bool Any() const
    {
        return resetReal || resetBot || keepBot || splitDecline || splitEntry || leaderCarryReal || leaderCarryBot;
    }
};

// The minute line, field order fixed (the contract pins it). Printed only in a minute with a non-zero field.
inline std::string MinuteLine(bool switchOn, Counts const& c)
{
    return "[Lockout] minute mode=" + std::string(switchOn ? "1" : "0") +
        " reset_real=" + std::to_string(c.resetReal) +
        " reset_bot=" + std::to_string(c.resetBot) +
        " keep_bot=" + std::to_string(c.keepBot) +
        " split_decline=" + std::to_string(c.splitDecline) +
        " split_entry=" + std::to_string(c.splitEntry) +
        " leader_carry_real=" + std::to_string(c.leaderCarryReal) +
        " leader_carry_bot=" + std::to_string(c.leaderCarryBot);
}
}
