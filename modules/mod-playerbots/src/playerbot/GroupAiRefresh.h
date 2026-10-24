#pragma once

#include <cstdint>
#include <string>

// twow-repo#628 / #627 (world-thread group managers: ad-hoc AdhocGroupWorld, S7 QuestGroupWorld): the world thread only
// changes the group itself (invite, accept, leave - the core's group opcodes and the unlocked group map). The AI part of
// that change - the strategy rebuild and the AI reset of AcceptInvitationAction / LeaveGroupAction, the S7 role sets - is
// posted to the bot and runs in its next UpdateAI on its own map thread (PlayerbotAI::ApplyGroupAiRequests). Short test
// #628 on C (24.10.2026): run on the world thread, one join or leave cost ~10 ms of the world tick (pass_us_max up to 81 ms).
namespace ai::group_ai_refresh
{
enum Request : std::uint32_t
{
    JoinReset  = 1u << 0,   // AcceptInvitationAction's AI part: movement strategy (free bot), default strategies, -lfg,-bg, reset
    LeaveReset = 1u << 1,   // LeaveGroupAction's AI part: default strategies, reset
    Follower   = 1u << 2,   // S7: the follower set (after a reset of the same request)
    TurnIn     = 1u << 3,   // S7: the turn-in set
};

// The event source of the managers' "accept invitation" (AdhocGroupWorld Join, QuestGroupWorld InviteAccept).
inline bool IsWorldManagerSource(std::string const& source)
{
    return source == "adhoc group" || source == "quest group";
}

// A request posted on top of the pending ones. The last group change wins: a join drops a pending leave and the S7 sets
// (the manager sets the role again after the join), a leave drops a pending join and the sets, the two S7 sets exclude
// each other.
inline std::uint32_t Merge(std::uint32_t pending, std::uint32_t add)
{
    if (add & JoinReset)
        pending &= ~std::uint32_t(LeaveReset | Follower | TurnIn);
    if (add & LeaveReset)
        pending &= ~std::uint32_t(JoinReset | Follower | TurnIn);
    if (add & Follower)
        pending &= ~std::uint32_t(TurnIn);
    if (add & TurnIn)
        pending &= ~std::uint32_t(Follower);
    return pending | add;
}

// What the bot thread does with the taken requests, in this order: a reset first (it would undo a set), then one set.
struct Plan
{
    bool reset = false;
    bool joinStrategies = false;   // the accept part: movement strategy before, -lfg,-bg after the default strategies
    bool follower = false;
    bool turnIn = false;
};

inline Plan PlanFor(std::uint32_t requests)
{
    Plan p;
    p.joinStrategies = (requests & JoinReset) != 0;
    p.reset = p.joinStrategies || (requests & LeaveReset) != 0;
    p.turnIn = (requests & TurnIn) != 0;
    p.follower = !p.turnIn && (requests & Follower) != 0;
    return p;
}
}
