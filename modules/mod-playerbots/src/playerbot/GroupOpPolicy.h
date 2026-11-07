#pragma once

#include "CohortPolicy.h"

#include <cstddef>
#include <cstdint>
#include <string>

// BOT-12 (twow-repo#677, #628; OB-00 04.11.2026; AiPlayerbot.BotGroups.OpQueue, default 0): the group changes of free bots run
// on the world thread. ObjectMgr::m_GroupMap is an unlocked std::unordered_map and group ids come from a plain m_nextGuid++;
// the core keeps "the group map changes only while the maps are idle" (player group opcodes are PACKET_PROCESS_WORLD), and the
// bot actions broke it by calling the group handlers on their own map thread. With OpQueue on for a bot, its ad-hoc leaves and
// joins are queued as plain ops (guids, no pointers: GroupOpQueue.h) and run in PlayerbotWorldScript::OnUpdate, after
// sMapMgr.Update, with all maps idle (GroupOps.cpp). Pure rules here, no game types, no config reads, no locks.
namespace ai::group_ops
{
// A3 lever 2 (ADR-0041, AiPlayerbot.BotGroups.WritePath): Accept, Invite and Leader are the write path's new kinds. The value
// is the FIFO index; the pop order is kPopOrder.
enum class OpKind : std::uint8_t
{
    Leave = 0,
    Join = 1,
    Accept = 2,   // the invitee accepts (or declines) a pending invite: bot = invitee, other = the invite's leader
    Invite = 3,   // bot = inviter, other = invitee, groupId = the inviter's group at the decision (0 = none)
    Leader = 4,   // bot = the leader, other = the new leader, groupId = its group
};
constexpr std::size_t kKinds = 5;

inline char const* KindName(OpKind kind)
{
    switch (kind)
    {
        case OpKind::Leave:  return "leave";
        case OpKind::Join:   return "join";
        case OpKind::Accept: return "accept";
        case OpKind::Invite: return "invite";
        case OpKind::Leader: return "leader";
    }
    return "leave";
}

// A3 lever 2: releases before formations - Leave, Leader, Accept, Invite, Join.
constexpr OpKind kPopOrder[kKinds] = { OpKind::Leave, OpKind::Leader, OpKind::Accept, OpKind::Invite, OpKind::Join };

// A two-bot op holds both bots in the queue (dedup, pending marks): Join (as before) and Invite.
inline bool HoldsBoth(OpKind kind)
{
    return kind == OpKind::Join || kind == OpKind::Invite;
}

// A3 lever 2 (generalised supersede): a Leave of a bot replaces its pending Join, Accept or Invite; a pending Leave or
// Leader refuses it.
inline bool LeaveSupersedes(OpKind pending)
{
    return pending == OpKind::Join || pending == OpKind::Accept || pending == OpKind::Invite;
}

// Who queued the op. AdhocAction = 0 (PR1); the rest are the A3 lever 2 producers (BotGroups.WritePath).
enum class OpSource : std::uint8_t
{
    AdhocAction = 0,
    Command,        // LeaveGroupAction::Leave from "leave", "uninvite", "party command" (and any other outer action)
    LeaveFarAway,   // LeaveGroupAction::Leave from "leave far away"
    Repop,          // RepopAction (was a raw Group::RemoveMember)
    Accept,         // AcceptInvitationAction
    Invite,         // InviteToGroupAction::Invite ("invite", "invite nearby", "invite guild")
    Leader,         // PassLeadershipToMasterAction ("leader", "give leader")
};

// Op.reason of the new kinds (Leave keeps the ad-hoc reason).
constexpr std::uint8_t kAcceptDecline = 1;      // Accept: the security decline (UninviteFromGroup)
constexpr std::uint8_t kInviteRaidConvert = 1;  // Invite: the caller's "5th member: convert to raid" ("invite nearby/guild")
constexpr std::uint8_t kLeaderReset = 1;        // Leader: the legacy strategy reset after the change (a random bot)

// ---- the switch -----------------------------------------------------------------------------------------------------------
// AiPlayerbot.BotGroups.OpQueue: 0 off (today, default), 1 all bots, 2 cohort B (odd low guid), 3 cohort A (even low guid).
// Any other value is 0 (the loader writes one error line). The mode applies to the bots whose groups change: the leaver, and
// either bot of a join (JoinQueueApplies).
inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

inline std::uint32_t SanitizeMode(std::uint32_t mode)
{
    return ValidMode(mode) ? mode : cohort::ModeOff;
}

inline bool QueueApplies(std::uint32_t mode, std::uint32_t guidLow)
{
    return cohort::Applies(SanitizeMode(mode), guidLow);
}

// An ad-hoc join is queued when the mode applies to EITHER bot (OB-00 04.11.: a queue bot's invites and joins never run on its
// map thread - the inviter's invite opcode writes the new group's invite list and the invitee's m_groupInvite, the invitee's
// accept creates the group). Only a pair of two bots outside the mode keeps the map-thread path. In cohort modes 2/3 a
// cross-cohort pair is therefore always queued (design 3.7 had "the invitee's cohort decides"; PREREG: cross-cohort pairs).
inline bool JoinQueueApplies(std::uint32_t mode, std::uint32_t inviterLow, std::uint32_t inviteeLow)
{
    return QueueApplies(mode, inviterLow) || QueueApplies(mode, inviteeLow);
}

// A3 lever 2 (ADR-0041 amendment 4.2, AiPlayerbot.BotGroups.WritePath, default 0): the same modes (ValidMode / SanitizeMode
// above; the loader writes one [GroupWrite] error line for an unknown value). The cohort part of "in scope"; the game side
// (GroupOps.cpp WritePathRoutes) adds: a bot AI without a real-player master, not in a battleground, not on the world thread,
// outside the "join"/"lfg" bypass scope.
inline bool WritePathApplies(std::uint32_t mode, std::uint32_t guidLow)
{
    return cohort::Applies(SanitizeMode(mode), guidLow);
}

// ---- limits (OB-00 04.11. addition a) -------------------------------------------------------------------------------------
constexpr std::uint32_t kOpsPerTickMin = 1;
constexpr std::uint32_t kOpsPerTickMax = 16;
constexpr std::uint32_t kMaxDepthMin = 16;
constexpr std::uint32_t kMaxDepthMax = 65536;
constexpr std::uint32_t kAlertAgeMin = 5;
constexpr std::uint32_t kAlertAgeMax = 3600;
// An op of a bot in a loading screen / teleport waits for it this long - counted from the first look that found it in transit,
// not from the enqueue (a backlog does not shorten the wait) - then it is dropped (drop_transit).
constexpr std::uint32_t kTransitMaxAgeMs = 30000;
// The ALERT check walks the whole queue (put-back transit ops sit at the back), at most this often.
constexpr std::uint32_t kAlertCheckIntervalMs = 1000;
// The drain looks at no more than this many entries per tick beyond the ops it executes (superseded, dropped, retried):
// a burst of dead entries is cleared in steps, never in one tick.
constexpr std::uint32_t kExtraLooksPerTick = 32;
// One ALERT line at most this often.
constexpr std::uint32_t kAlertIntervalMs = 60000;

inline std::uint32_t ClampOpsPerTick(std::int64_t v)
{
    return v < kOpsPerTickMin ? kOpsPerTickMin : (v > kOpsPerTickMax ? kOpsPerTickMax : std::uint32_t(v));
}

inline std::uint32_t ClampMaxDepth(std::int64_t v)
{
    return v < kMaxDepthMin ? kMaxDepthMin : (v > kMaxDepthMax ? kMaxDepthMax : std::uint32_t(v));
}

inline std::uint32_t ClampAlertAgeSeconds(std::int64_t v)
{
    return v < kAlertAgeMin ? kAlertAgeMin : (v > kAlertAgeMax ? kAlertAgeMax : std::uint32_t(v));
}

// ---- the tick gate ----------------------------------------------------------------------------------------------------------
// Ops this world tick may execute: OpsPerTick, none in a tick in which the ad-hoc manager (AdHoc.WorldThread) or S7 already
// changed a group (MarkHeavyTick) - the #736 OneHeavyStep rule widened to all group work of one world tick.
inline std::uint32_t OpsThisTick(std::uint32_t opsPerTick, bool heavyTaken)
{
    return heavyTaken ? 0u : ClampOpsPerTick(opsPerTick);
}

// Entries the drain may take off the queue this tick (executed ops included).
inline std::uint32_t LooksThisTick(std::uint32_t opsThisTick)
{
    return opsThisTick ? opsThisTick + kExtraLooksPerTick : 0u;
}

// ---- the ALERT line -----------------------------------------------------------------------------------------------------------
inline bool AlertDue(bool hasOldest, std::uint32_t oldestAgeMs, std::uint32_t alertAgeSeconds, bool alerted,
    std::uint32_t nowMs, std::uint32_t lastAlertMs)
{
    if (!hasOldest || oldestAgeMs <= alertAgeSeconds * 1000u)
        return false;
    return !alerted || nowMs - lastAlertMs >= kAlertIntervalMs;
}

// ---- the age distribution of the minute line (design 4.3 age_ms_p99) -------------------------------------------------------
// Fixed buckets by upper bound (inclusive, ms); the last bucket is everything above. 500 is an edge, so the PREREG's
// "age_ms_p99 <= 500" is decided exactly.
constexpr std::uint32_t kAgeEdgesMs[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 30000, 60000 };
constexpr std::size_t kAgeBuckets = sizeof(kAgeEdgesMs) / sizeof(kAgeEdgesMs[0]) + 1;

inline std::size_t AgeBucket(std::uint32_t ageMs)
{
    std::size_t i = 0;
    while (i + 1 < kAgeBuckets && ageMs > kAgeEdgesMs[i])
        ++i;
    return i;
}

// The 99th percentile as the upper edge of its bucket, never above the minute's max (the overflow bucket reports the max).
// 0 with no op.
inline std::uint32_t AgeP99Ms(std::uint32_t const (&hist)[kAgeBuckets], std::uint32_t ageMsMax)
{
    std::uint64_t n = 0;
    for (std::uint32_t c : hist)
        n += c;
    if (!n)
        return 0;
    std::uint64_t const rank = (n * 99 + 99) / 100;   // ceil(0.99 n)
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < kAgeBuckets; ++i)
    {
        seen += hist[i];
        if (seen >= rank)
            return i + 1 < kAgeBuckets && kAgeEdgesMs[i] < ageMsMax ? kAgeEdgesMs[i] : ageMsMax;
    }
    return ageMsMax;
}

// ---- staleness (design section 3.4), decided on the world thread with the maps idle ------------------------------------------
enum class Verdict : std::uint8_t
{
    Execute = 0,
    Retry,             // in a loading screen / teleport: back into the queue, next tick
    DropLoggedOut,     // the bot (or the inviter) is gone or has no session
    DropTransit,       // still in transit after kTransitMaxAgeMs
    DropNotFree,       // a real player is its master now (PR1 is free bots only)
    DropBg,            // in a battleground (HandleGroupDisbandOpcode returns early there)
    DropAlreadyLeft,   // Leave: no group any more
    DropGroupChanged,  // Leave: in another group than the one it decided to leave
    DropStalePair,     // Join: the pair or the joining group changed since the decision
};
constexpr std::size_t kVerdicts = 9;

inline char const* VerdictName(Verdict v)
{
    switch (v)
    {
        case Verdict::Execute:          return "execute";
        case Verdict::Retry:            return "retry_transit";
        case Verdict::DropLoggedOut:    return "drop_logged_out";
        case Verdict::DropTransit:      return "drop_transit";
        case Verdict::DropNotFree:      return "drop_not_free";
        case Verdict::DropBg:           return "drop_bg";
        case Verdict::DropAlreadyLeft:  return "drop_already_left";
        case Verdict::DropGroupChanged: return "drop_group_changed";
        case Verdict::DropStalePair:    return "drop_stale_pair";
    }
    return "unknown";
}

inline bool IsDrop(Verdict v)
{
    return v != Verdict::Execute && v != Verdict::Retry;
}

struct LeaveOpFacts
{
    bool found = false;            // in the world's player map with a session and an AI
    bool inWorld = false;
    bool teleporting = false;
    bool realMaster = false;
    bool inBattleground = false;
    std::uint32_t groupId = 0;     // its group now (0 = none)
    std::uint32_t opGroupId = 0;   // the group it decided to leave
    std::uint32_t transitMs = 0;   // ms in transit, from the first look that found it so (0 at that look)
};

inline Verdict DecideLeaveOp(LeaveOpFacts const& f)
{
    if (!f.found)
        return Verdict::DropLoggedOut;
    if (!f.inWorld || f.teleporting)
        return f.transitMs >= kTransitMaxAgeMs ? Verdict::DropTransit : Verdict::Retry;
    if (f.realMaster)
        return Verdict::DropNotFree;
    if (f.inBattleground)
        return Verdict::DropBg;
    if (!f.groupId)
        return Verdict::DropAlreadyLeft;
    if (f.groupId != f.opGroupId)
        return Verdict::DropGroupChanged;
    return Verdict::Execute;
}

struct JoinOpFacts
{
    bool found = false;               // invitee and inviter, both
    bool inWorld = false;             // both
    bool teleporting = false;         // either
    bool realMaster = false;          // either
    bool inBattleground = false;      // either
    bool inviteeGrouped = false;
    bool inviteeInvited = false;      // a pending invite of anyone
    std::uint32_t inviterGroupId = 0; // the inviter's group now (0 = none)
    bool inviterLeads = false;
    std::uint32_t inviterMembers = 0;
    std::uint32_t maxMembers = 0;     // BotGroups.MaxBots
    std::uint32_t opGroupId = 0;      // the joining group at the decision (0 = a new group)
    bool sameMap = false;
    bool inRange = false;             // within 2 x AdHoc.Radius
    std::uint32_t transitMs = 0;   // ms in transit, from the first look that found it so (0 at that look)
};

inline Verdict DecideJoinOp(JoinOpFacts const& f)
{
    if (!f.found)
        return Verdict::DropLoggedOut;
    if (!f.inWorld || f.teleporting)
        return f.transitMs >= kTransitMaxAgeMs ? Verdict::DropTransit : Verdict::Retry;
    if (f.realMaster)
        return Verdict::DropNotFree;
    if (f.inBattleground)
        return Verdict::DropBg;
    if (f.inviteeGrouped || f.inviteeInvited)
        return Verdict::DropStalePair;
    if (f.opGroupId ? (f.inviterGroupId != f.opGroupId || !f.inviterLeads || f.inviterMembers >= f.maxMembers)
                    : f.inviterGroupId != 0)
        return Verdict::DropStalePair;
    if (!f.sameMap || !f.inRange)
        return Verdict::DropStalePair;
    return Verdict::Execute;
}

// ---- the off-thread observer (OB-00 04.11. addition b) ---------------------------------------------------------------------
// Group changes seen OFF the world thread, by the calling path: the outermost bot action running on that thread
// (Engine::ListenAndExecute), so the run shows what is left. off_map_<path> counts the m_GroupMap writes (Group::Create ->
// ObjectMgr::AddGroup, disband -> RemoveGroup) of every bot; off_q_<path> counts every group change (create, disband, add,
// remove) made by a bot OpQueue applies to - PR1 primary: off_q_adhoc = 0 in every mode.
enum class OffSource : std::uint8_t
{
    Adhoc = 0,       // "ad-hoc group", and the invitee's "accept invitation" of an ad-hoc invite (the accept creates the group; PR1)
    LeaveFar,        // "leave far away" (PR2)
    Repop,           // "repop", "release", "auto release" (PR2; repop's raw RemoveMember)
    Accept,          // "accept invitation" of any other invite (PR2 bot invites, PR3 a real player's invite)
    InviteLfg,       // "invite nearby", "invite guild", "invite", "join", "lfg*" (PR2)
    Command,         // "leave", "uninvite", "party command", "leader", "give leader" (PR3)
    DungeonClear,    // "dungeon clear *", "dc *" (mod-dungeon-clear actions)
    OtherAction,     // any other bot action
    Other,           // no bot action on this thread (core, command, other module)
};
constexpr std::size_t kOffSources = 9;

inline char const* OffSourceName(OffSource s)
{
    switch (s)
    {
        case OffSource::Adhoc:        return "adhoc";
        case OffSource::LeaveFar:     return "leave_far";
        case OffSource::Repop:        return "repop";
        case OffSource::Accept:       return "accept";
        case OffSource::InviteLfg:    return "invite_lfg";
        case OffSource::Command:      return "command";
        case OffSource::DungeonClear: return "dungeon_clear";
        case OffSource::OtherAction:  return "other_action";
        case OffSource::Other:        return "other";
    }
    return "other";
}

inline bool StartsWith(std::string const& s, char const* prefix)
{
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

// hasAction false: no bot action runs on this thread. adhocInvite: the group's leader is the inviter of the ad-hoc invite the
// acting bot got last (AdhocGroupAction::SendInvite tags the invitee, GroupOps.cpp NoteAdhocInvite) - the accept of that invite
// is the ad-hoc path's group create, not a PR2/PR3 accept.
inline OffSource OffSourceOf(bool hasAction, std::string const& action, bool adhocInvite = false)
{
    if (!hasAction)
        return OffSource::Other;
    if (action == "ad-hoc group")
        return OffSource::Adhoc;
    if (action == "leave far away")
        return OffSource::LeaveFar;
    if (action == "repop" || action == "release" || action == "auto release")
        return OffSource::Repop;
    if (action == "accept invitation")
        return adhocInvite ? OffSource::Adhoc : OffSource::Accept;
    if (action == "invite nearby" || action == "invite guild" || action == "invite" || action == "join" || StartsWith(action, "lfg"))
        return OffSource::InviteLfg;
    if (action == "leave" || action == "uninvite" || action == "party command" || action == "leader" || action == "give leader")
        return OffSource::Command;
    if (StartsWith(action, "dungeon clear") || StartsWith(action, "dc "))
        return OffSource::DungeonClear;
    return OffSource::OtherAction;
}

// A3 lever 2: the source of a queued LeaveGroupAction::Leave, by the outermost action on the thread (OffSourceOf's names):
// "leave far away", "repop"/"release", the ad-hoc action; every other caller ("leave", "uninvite", "party command", ...) is a
// command.
inline OpSource LeaveSourceOf(std::string const& outerAction)
{
    switch (OffSourceOf(true, outerAction))
    {
        case OffSource::LeaveFar: return OpSource::LeaveFarAway;
        case OffSource::Repop:    return OpSource::Repop;
        case OffSource::Adhoc:    return OpSource::AdhocAction;
        default:                  return OpSource::Command;
    }
}

// ---- A3 lever 2: staleness of the write path's new kinds, decided on the world thread (GroupOps.cpp) ------------------------
// The existing verdicts only (the [GroupOps] minute line prints every verdict: a new one would change its fields).
struct AcceptOpFacts
{
    bool found = false;          // the invitee (session, AI) and the invite's leader (session)
    bool inWorld = false;        // both
    bool teleporting = false;    // either
    bool realMaster = false;     // the invitee
    bool invited = false;        // the invitee still has a pending invite whose leader is op.other
    std::uint32_t transitMs = 0;
};

inline Verdict DecideAcceptOp(AcceptOpFacts const& f)
{
    if (!f.found)
        return Verdict::DropLoggedOut;
    if (!f.inWorld || f.teleporting)
        return f.transitMs >= kTransitMaxAgeMs ? Verdict::DropTransit : Verdict::Retry;
    if (f.realMaster)
        return Verdict::DropNotFree;
    if (!f.invited)
        return Verdict::DropStalePair;
    return Verdict::Execute;
}

struct InviteOpFacts
{
    bool found = false;                // the inviter (session, AI) and the invitee (session; a real player too)
    bool inWorld = false;              // both
    bool teleporting = false;          // either
    bool realMaster = false;           // the inviter
    bool inBattleground = false;       // either
    bool inviteeGrouped = false;
    bool inviteeInvited = false;       // a pending invite of anyone
    std::uint32_t inviterGroupId = 0;  // the inviter's group now (0 = none)
    std::uint32_t opGroupId = 0;       // the inviter's group at the decision (0 = none)
    bool groupFull = false;            // the inviter's group is full (after the raid conversion the legacy code would do)
    std::uint32_t transitMs = 0;
};

inline Verdict DecideInviteOp(InviteOpFacts const& f)
{
    if (!f.found)
        return Verdict::DropLoggedOut;
    if (!f.inWorld || f.teleporting)
        return f.transitMs >= kTransitMaxAgeMs ? Verdict::DropTransit : Verdict::Retry;
    if (f.realMaster)
        return Verdict::DropNotFree;
    if (f.inBattleground)
        return Verdict::DropBg;
    if (f.inviteeGrouped || f.inviteeInvited)
        return Verdict::DropStalePair;
    if (f.inviterGroupId != f.opGroupId || f.groupFull)
        return Verdict::DropStalePair;
    return Verdict::Execute;
}

struct LeaderOpFacts
{
    bool found = false;            // the leader (session, AI) and the new leader (session)
    bool inWorld = false;          // both
    bool teleporting = false;      // either
    bool realMaster = false;       // the leader
    bool inBattleground = false;   // the leader
    std::uint32_t groupId = 0;     // the leader's group now (0 = none)
    std::uint32_t opGroupId = 0;   // its group at the decision
    bool leads = false;            // op.bot still leads it
    bool otherMember = false;      // the new leader is still a member (the core's own check: the same group)
    std::uint32_t transitMs = 0;
};

inline Verdict DecideLeaderOp(LeaderOpFacts const& f)
{
    if (!f.found)
        return Verdict::DropLoggedOut;
    if (!f.inWorld || f.teleporting)
        return f.transitMs >= kTransitMaxAgeMs ? Verdict::DropTransit : Verdict::Retry;
    if (f.realMaster)
        return Verdict::DropNotFree;
    if (f.inBattleground)
        return Verdict::DropBg;
    if (!f.groupId)
        return Verdict::DropAlreadyLeft;
    if (f.groupId != f.opGroupId)
        return Verdict::DropGroupChanged;
    if (!f.leads || !f.otherMember)
        return Verdict::DropStalePair;
    return Verdict::Execute;
}
}
