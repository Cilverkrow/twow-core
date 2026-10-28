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
    // S7 QuestGroups.StagedOps (S7-A 25.10.: a formation of 2 cost 2.9-4.3 ms of the world tick): the forced quest travel
    // (FindQuestDest + SetTarget) on the bot's own thread, after the set; quest and objective in the bot's questGroupForce.
    ForceTravel = 1u << 4,
    // s7r (AiPlayerbot.BotGroups.WorldThreadDefer): the rest of AcceptInvitationAction after the core join - event and membership
    // log, invite chat, formation, hello, reset raids, update gear - on the bot's own thread (after the reset).
    JoinTail = 1u << 5,
};

// The event source of the managers' "accept invitation" (AdhocGroupWorld Join, QuestGroupWorld InviteAccept).
inline bool IsWorldManagerSource(std::string const& source)
{
    return source == "adhoc group" || source == "quest group";
}

// s7r: the event source of the posted accept tail (AcceptInvitationAction runs only its tail for it).
inline constexpr char const* JoinTailSource = "join tail";

// A request posted on top of the pending ones. The last group change wins: a join drops a pending leave and the S7 sets
// (the manager sets the role again after the join), a leave drops a pending join and the sets, the two S7 sets exclude
// each other.
inline std::uint32_t Merge(std::uint32_t pending, std::uint32_t add)
{
    if (add & JoinReset)
        pending &= ~std::uint32_t(LeaveReset | Follower | TurnIn | ForceTravel);
    if (add & LeaveReset)
        pending &= ~std::uint32_t(JoinReset | JoinTail | Follower | TurnIn | ForceTravel);
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
    bool joinTail = false;      // after the reset, before the set
    bool forceTravel = false;   // last: after the reset and the set
};

inline Plan PlanFor(std::uint32_t requests)
{
    Plan p;
    p.joinStrategies = (requests & JoinReset) != 0;
    p.reset = p.joinStrategies || (requests & LeaveReset) != 0;
    p.turnIn = (requests & TurnIn) != 0;
    p.follower = !p.turnIn && (requests & Follower) != 0;
    p.joinTail = (requests & JoinTail) != 0;
    p.forceTravel = (requests & ForceTravel) != 0;
    return p;
}

// The forced quest travel's parameters in one word (PlayerbotAI::questGroupForce): quest id and objective index -1..6.
inline std::uint32_t PackForce(std::uint32_t questId, int objIdx)
{
    return (questId << 3) | std::uint32_t(objIdx + 1);
}

inline void UnpackForce(std::uint32_t packed, std::uint32_t& questId, int& objIdx)
{
    questId = packed >> 3;
    objIdx = int(packed & 7u) - 1;
}
}
