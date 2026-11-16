#pragma once

#include "GroupDirectivePolicy.h"
#include "ManagerInboxPolicy.h"
#include "SupervisorPolicy.h"

#include <cstddef>
#include <cstdint>

// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0): the pure rules of the bot
// side of the group manager (design A3-S3-T02-ENTWURF.md v0.4.3 section 7, build spec A3-S3-T02GROUP-BAUSPEC.md 2.2 with the
// recheck of 11.11.). The check order and the ack per verb, the R4 choice of the newest directive, the orphan clock, the goal end,
// the R-c class of the live target, the move kind, the follower step, the close ring, the self-exit lock and the left retry. No
// game type, no config, no lock, no log: SupervisorGroup.cpp feeds the facts in and does the writes. Included only by
// SupervisorGroup.cpp and t/supervisor_group_policy_tests.cpp, never by Supervisor.h (whose members stay plain fields; the
// functions below that keep state take those fields by reference, the structs are the test's view of the same fields).
// One source for states and reasons: kAckApplied / kAckRefused (ManagerInboxPolicy.h), the kAckReason constants
// (GroupDirectivePolicy.h) and std::uint8_t(Deny) for 1-7; no number literal stands for an ack state or reason here.
namespace ai::supervisor
{
// ---- clocks ----------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kGroupCheckMs = 1000;    // goal end, goal_close, follower check, adoption: at most once per second
constexpr std::uint32_t kOrphanAskMs = 10000;    // the one view question of the handler: at most once per 10 s

// now has reached at on the 32-bit ms clock (unsigned difference, wraps).
inline bool ClockReached(std::uint32_t nowMs, std::uint32_t atMs)
{
    return std::uint32_t(nowMs - atMs) < 0x80000000u;
}

// A period gate on one plain field: true when due (nextMs 0 = due at once), then the next is periodMs later (never 0).
inline bool PeriodDue(std::uint32_t& nextMs, std::uint32_t nowMs, std::uint32_t periodMs)
{
    if (nextMs != 0 && !ClockReached(nowMs, nextMs))
        return false;
    nextMs = nowMs + periodMs;
    if (nextMs == 0)
        nextMs = 1;
    return true;
}

// ---- verbs (design 7.4 HandleMail, 7.5) ---------------------------------------------------------------------------------------
// The handler names the verbs by class only: Enter, Change (the T33 verb 2), Exit; None and 4-15 are Bad (the manager never
// sends them, UnpackDirective reads b0-3 unchecked).
enum class VerbClass : std::uint8_t
{
    Bad,
    Enter,
    Change,
    Exit,
};

inline VerbClass ClassifyVerb(group_mgr::Verb v)
{
    std::uint8_t const raw = std::uint8_t(v);
    if (raw == std::uint8_t(group_mgr::Verb::Enter))
        return VerbClass::Enter;
    if (raw == std::uint8_t(group_mgr::Verb::Exit))
        return VerbClass::Exit;
    if (raw > std::uint8_t(group_mgr::Verb::Enter) && raw < std::uint8_t(group_mgr::Verb::Exit))
        return VerbClass::Change;
    return VerbClass::Bad;
}

// R4: only the newest GroupDirective of one drain counts. Returns its index (n = none); superseded = the older directives (no
// ack for them).
inline std::size_t NewestDirective(Event const* mail, std::size_t n, std::size_t& superseded)
{
    std::size_t newest = n;
    std::size_t directives = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (mail[i].kind != EventKind::GroupDirective)
            continue;
        newest = i;
        ++directives;
    }
    superseded = directives ? directives - 1 : 0;
    return newest;
}

// ---- acks (design 7.5) ------------------------------------------------------------------------------------------------------
struct AckDecision
{
    std::uint8_t state;
    std::uint8_t reason;
};

inline AckDecision AckOk()
{
    return AckDecision{manager_inbox::kAckApplied, std::uint8_t(Deny::None)};
}

inline AckDecision AckNo(std::uint8_t reason)
{
    return AckDecision{manager_inbox::kAckRefused, reason};
}

inline AckDecision AckNo(Deny reason)
{
    return AckNo(std::uint8_t(reason));
}

inline bool AckApplied(AckDecision a)
{
    return a.state == manager_inbox::kAckApplied;
}

// A verb 0 or 4-15: 3/2 NotAMode, no state change, no write.
inline AckDecision BadVerbAck()
{
    return AckNo(Deny::NotAMode);
}

// The facts of an Enter, read by the caller on the bot thread. inGroup = mode GROUP (with its held group); sameSerial = the held
// group has the directive's serial; roleNotByHandler = the bot's own quest group role is not 0 and the handler did not set it;
// startZoneHold = the start zone holds at the event's anchor (R8); goalInLog/goalDone/goalTurnedIn = c in the quest log, its
// objectives complete, already turned in; focusQuest = the player focus quest (0 = none); c = the directive's quest id.
struct EnterFacts
{
    bool arbiter2 = false;
    bool handlerOn = false;
    std::uint8_t type = 0;
    bool inGroup = false;
    bool sameSerial = false;
    bool inRescue = false;
    bool roleNotByHandler = false;
    bool startZoneHold = false;
    bool goalInLog = false;
    bool goalDone = false;
    bool goalTurnedIn = false;
    std::uint32_t focusQuest = 0;
    std::uint32_t c = 0;
};

// The checks of an Enter in this order: 3/22 arbiter_off (Arbiter != 2 or the handler switch off), 3/2 (a type other than
// Adhoc and Quest), 3/7 (RESCUE), the idempotent 2/0 (GROUP, same serial), then (1) 3/21 legacy_group_role, (2) 3/16
// start_zone_hold, (3) 3/24 goal_not_in_log (fail closed: c 0, not in the log, complete or turned in), (4) 3/25 focus_conflict.
// 2/0 otherwise: outside GROUP the caller then asks Decide (AckFromDecide), in GROUP with another serial it moves.
inline AckDecision EnterCheck(EnterFacts const& f)
{
    if (!f.arbiter2 || !f.handlerOn)
        return AckNo(group_mgr::kAckReasonArbiterOff);
    if (f.type != std::uint8_t(group_mgr::GroupType::Adhoc) && f.type != std::uint8_t(group_mgr::GroupType::Quest))
        return AckNo(Deny::NotAMode);
    if (f.inRescue)
        return AckNo(Deny::RescueFirst);
    if (f.inGroup && f.sameSerial)
        return AckOk();
    if (f.roleNotByHandler)
        return AckNo(group_mgr::kAckReasonLegacyGroupRole);
    if (f.startZoneHold)
        return AckNo(group_mgr::kAckReasonStartZoneHold);
    if (f.c == 0 || !f.goalInLog || f.goalDone || f.goalTurnedIn)
        return AckNo(group_mgr::kAckReasonGoalNotInLog);
    if (f.focusQuest != 0 && f.focusQuest != f.c)
        return AckNo(group_mgr::kAckReasonFocusConflict);
    return AckOk();
}

// GROUP with the same serial: nothing to do, 2/0.
inline bool EnterIdempotent(EnterFacts const& f)
{
    return f.inGroup && f.sameSerial && AckApplied(EnterCheck(f));
}

// GROUP with another serial: the move (checks (1)-(4) without Decide; refused = leave the old group first).
inline bool EnterIsMove(EnterFacts const& f)
{
    return f.inGroup && !f.sameSerial;
}

// Decide(cur, Group, GroupMgr) as an ack: allowed 2/0, else 3/<Deny> (PassiveHold 4, Phase 6, RescueFirst 7, ...).
inline AckDecision AckFromDecide(TransitionResult const& r)
{
    return r.allowed ? AckOk() : AckNo(r.reason);
}

// The facts of a Change (T33): pointOtherMap / pointStartZoneHold only for a gather point (gatherFlag 1, kind b or c), else false.
struct ChangeFacts
{
    bool arbiter2 = false;
    bool handlerOn = false;
    bool inGroup = false;
    bool sameSerial = false;
    bool pointOtherMap = false;
    bool pointStartZoneHold = false;
};

// 3/22 arbiter_off, 3/20 no_open_group (not in GROUP, also RESCUE, or another serial), 3/19 gather_other_map, 3/16
// start_zone_hold, else 2/0.
inline AckDecision ChangeCheck(ChangeFacts const& f)
{
    if (!f.arbiter2 || !f.handlerOn)
        return AckNo(group_mgr::kAckReasonArbiterOff);
    if (!f.inGroup || !f.sameSerial)
        return AckNo(group_mgr::kAckReasonNoOpenGroup);
    if (f.pointOtherMap)
        return AckNo(group_mgr::kAckReasonGatherOtherMap);
    if (f.pointStartZoneHold)
        return AckNo(group_mgr::kAckReasonStartZoneHold);
    return AckOk();
}

// An Exit is always 2/0. ExitApplied: GROUP with the same serial (leave, Decide(Group, Quest, GroupMgr)); ExitInRescue: RESCUE
// with a held group of the same serial (the RESCUE branch of the leave, no Decide while dead); ExitStale: anything else.
enum class ExitClass : std::uint8_t
{
    ExitApplied,
    ExitInRescue,
    ExitStale,
};

struct ExitDecision
{
    AckDecision ack;
    ExitClass kind;
};

inline ExitDecision ExitCheck(bool inGroup, bool inRescueHeld, bool sameSerial)
{
    if (inGroup && sameSerial)
        return ExitDecision{AckOk(), ExitClass::ExitApplied};
    if (inRescueHeld && sameSerial)
        return ExitDecision{AckOk(), ExitClass::ExitInRescue};
    return ExitDecision{AckOk(), ExitClass::ExitStale};
}

// ---- counters (design 7.11): which GrpField a directive, its ack and its reason count ------------------------------------------
inline GrpField DirField(VerbClass v)
{
    switch (v)
    {
        case VerbClass::Enter: return GrpField::DirEnter;
        case VerbClass::Change: return GrpField::DirUpdate;
        case VerbClass::Exit: return GrpField::DirExit;
        case VerbClass::Bad: break;
    }
    return GrpField::DirBad;
}

// The decided ack (also when its post fails): enter_ok/enter_ref, update_ok/update_ref, exit_ok/exit_stale, bad_ref.
inline GrpField AckOutcomeField(VerbClass v, AckDecision a, ExitClass exitKind)
{
    switch (v)
    {
        case VerbClass::Enter: return AckApplied(a) ? GrpField::EnterOk : GrpField::EnterRef;
        case VerbClass::Change: return AckApplied(a) ? GrpField::UpdateOk : GrpField::UpdateRef;
        case VerbClass::Exit: return exitKind == ExitClass::ExitStale ? GrpField::ExitStale : GrpField::ExitOk;
        case VerbClass::Bad: break;
    }
    return GrpField::BadRef;
}

// A failed ack post: ack_drop_exit for the ack of an Exit, ack_drop_enter for every other (Enter, Change, a bad verb).
inline GrpField AckDropField(VerbClass v)
{
    return v == VerbClass::Exit ? GrpField::AckDropExit : GrpField::AckDropEnter;
}

// The reason field of a refusal: r_deny for 1-7, r16, r19-r25; false for 0 and every other value.
inline bool ReasonField(std::uint8_t reason, GrpField& out)
{
    if (reason >= std::uint8_t(Deny::SameMode) && reason <= std::uint8_t(Deny::RescueFirst))
    {
        out = GrpField::RDeny;
        return true;
    }
    switch (reason)
    {
        case group_mgr::kAckReasonStartZoneHold: out = GrpField::R16; return true;
        case group_mgr::kAckReasonGatherOtherMap: out = GrpField::R19; return true;
        case group_mgr::kAckReasonNoOpenGroup: out = GrpField::R20; return true;
        case group_mgr::kAckReasonLegacyGroupRole: out = GrpField::R21; return true;
        case group_mgr::kAckReasonArbiterOff: out = GrpField::R22; return true;
        case group_mgr::kAckReasonLaneBusy: out = GrpField::R23; return true;
        case group_mgr::kAckReasonGoalNotInLog: out = GrpField::R24; return true;
        case group_mgr::kAckReasonFocusConflict: out = GrpField::R25; return true;
        default: break;
    }
    return false;
}

// The run equation of design 7.11 (R9 veto): L = enter_ok + enter_ref + update_ok + update_ref + exit_ok + exit_stale + bad_ref,
// R = dir_enter + dir_update + dir_exit + dir_bad - superseded; over a run 0 <= R - L <= R of the last printed minute.
inline std::int64_t GrpRight(ArbiterGrpMinute const& g)
{
    auto const v = [&g](GrpField f) { return std::int64_t(g.v[std::size_t(f)]); };
    return v(GrpField::DirEnter) + v(GrpField::DirUpdate) + v(GrpField::DirExit) + v(GrpField::DirBad) - v(GrpField::Superseded);
}

inline std::int64_t GrpLeft(ArbiterGrpMinute const& g)
{
    auto const v = [&g](GrpField f) { return std::int64_t(g.v[std::size_t(f)]); };
    return v(GrpField::EnterOk) + v(GrpField::EnterRef) + v(GrpField::UpdateOk) + v(GrpField::UpdateRef) + v(GrpField::ExitOk) +
        v(GrpField::ExitStale) + v(GrpField::BadRef);
}

inline bool GrpBalanced(ArbiterGrpMinute const& sum, ArbiterGrpMinute const& last)
{
    std::int64_t const open = GrpRight(sum) - GrpLeft(sum);
    return open >= 0 && open <= GrpRight(last);
}

// ---- the arbiter / handler switch (design 7.4 step 3) ---------------------------------------------------------------------------
// A held group (GROUP, or RESCUE with a remembered group) is left silently when Arbiter != 2 (ArbiterOff, the one tick of a
// switch away from 2) or when GroupHandlerOn is false (HandlerOff, counted handler_off; unreachable under Q4 (a)). No left (Q5).
enum class HandlerLeave : std::uint8_t
{
    None,
    ArbiterOff,
    HandlerOff,
};

inline HandlerLeave HandlerLeaveCause(bool arbiter2, bool handlerOn, bool groupHeld)
{
    if (!groupHeld)
        return HandlerLeave::None;
    if (!arbiter2)
        return HandlerLeave::ArbiterOff;
    return handlerOn ? HandlerLeave::None : HandlerLeave::HandlerOff;
}

// ---- RESCUE (design 7.4 step 7, 7.5) ------------------------------------------------------------------------------------------
// Alive again in RESCUE that this handler entered (rescueByGroup): with a held group the fresh view answer decides (ToGroup
// counts rescue_group; ToQuestLeave leaves the group, rescue_back); without a held group back to Quest (rescue_back, ev=mode
// cause rescue_end). Stay: not this handler's RESCUE, or still dead.
enum class RescueEnd : std::uint8_t
{
    Stay,
    ToGroup,
    ToQuestLeave,
    ToQuestNoGroup,
};

inline RescueEnd RescueReturn(bool rescueByGroup, bool alive, bool groupHeld, bool managed)
{
    if (!rescueByGroup || !alive)
        return RescueEnd::Stay;
    if (!groupHeld)
        return RescueEnd::ToQuestNoGroup;
    return managed ? RescueEnd::ToGroup : RescueEnd::ToQuestLeave;
}

// ---- orphan clock (design 7.9) ----------------------------------------------------------------------------------------------
// falseSinceMs: since when the view answers false (0 = it did not); nextAskMs: the next view question (0 = at once). Reset on
// every RESCUE entry and exit and on Enter, so the first question after a revival is fresh.
struct OrphanClock
{
    std::uint32_t falseSinceMs = 0;
    std::uint32_t nextAskMs = 0;
};

// The 10 s question gate (the caller asks the view only when this is true).
inline bool OrphanAskTake(std::uint32_t& nextAskMs, std::uint32_t nowMs)
{
    return PeriodDue(nextAskMs, nowMs, kOrphanAskMs);
}

inline bool OrphanAskTake(OrphanClock& clock, std::uint32_t nowMs)
{
    return OrphanAskTake(clock.nextAskMs, nowMs);
}

// One answer of the view: true once it was false for kOrphanGraceMs (GroupManagerPolicy.h) in a row.
inline bool OrphanStep(std::uint32_t& falseSinceMs, std::uint32_t nowMs, bool managed)
{
    if (managed)
    {
        falseSinceMs = 0;
        return false;
    }
    if (falseSinceMs == 0)
    {
        falseSinceMs = nowMs ? nowMs : 1;
        return false;
    }
    return std::uint32_t(nowMs - falseSinceMs) >= group_mgr::kOrphanGraceMs;
}

inline bool OrphanStep(OrphanClock& clock, std::uint32_t nowMs, bool managed)
{
    return OrphanStep(clock.falseSinceMs, nowMs, managed);
}

inline void OrphanReset(std::uint32_t& falseSinceMs, std::uint32_t& nextAskMs)
{
    falseSinceMs = 0;
    nextAskMs = 0;
}

inline void OrphanReset(OrphanClock& clock)
{
    OrphanReset(clock.falseSinceMs, clock.nextAskMs);
}

// RESCUE -> GROUP after the fresh view question of the revival: the false run starts over, and that question counts as the ask
// of its window (the next one is due kOrphanAskMs later, not in the next pass).
inline void OrphanResetAsked(std::uint32_t& falseSinceMs, std::uint32_t& nextAskMs, std::uint32_t nowMs)
{
    OrphanReset(falseSinceMs, nextAskMs);
    PeriodDue(nextAskMs, nowMs, kOrphanAskMs);
}

// ---- goal end (design 7.9) --------------------------------------------------------------------------------------------------
enum class GoalEnd : std::uint8_t
{
    None,
    Done,
    Lost,
};

inline GoalEnd ClassifyGoal(bool turnedIn, bool inLog)
{
    if (turnedIn)
        return GoalEnd::Done;
    return inLog ? GoalEnd::None : GoalEnd::Lost;
}

// The left reason of a goal end: 26 goal_done, 27 goal_lost (the only reasons of a left, Q5 (a)).
inline std::uint8_t GoalEndReason(GoalEnd end)
{
    return end == GoalEnd::Done ? group_mgr::kAckReasonGoalDone : group_mgr::kAckReasonGoalLost;
}

// ---- R-c: the live target against the group goal c (design 7.7) ----------------------------------------------------------------
// None: no target or not open; Prepare: a search started before (status PREPARE); QuestOther / QuestGoal: an open quest
// destination of another quest / of c; NonQuest: any other open target (grind, gather, vendor, ...; allowed, sampled nonquest).
enum class LiveClass : std::uint8_t
{
    None,
    Prepare,
    QuestOther,
    QuestGoal,
    NonQuest,
};

inline LiveClass ClassifyLive(bool hasTarget, bool prepare, bool open, bool questDest, std::uint32_t questId, std::uint32_t c)
{
    if (!hasTarget)
        return LiveClass::None;
    if (prepare)
        return LiveClass::Prepare;
    if (!open)
        return LiveClass::None;
    if (!questDest)
        return LiveClass::NonQuest;
    return questId == c ? LiveClass::QuestGoal : LiveClass::QuestOther;
}

// What R-c closes. At an entry (Enter, accepted move, RESCUE -> GROUP): an open quest target != c expires (cause group_goal,
// goal_close) and a PREPARE is cancelled (status none, cause group_prepare, prep_cancel). The once-per-second check in GROUP
// closes only the quest target != c (goal_close).
enum class RcClose : std::uint8_t
{
    None,
    Expire,
    Cancel,
};

inline RcClose RcCheck(LiveClass live, bool atEntry)
{
    if (live == LiveClass::QuestOther)
        return RcClose::Expire;
    if (live == LiveClass::Prepare && atEntry)
        return RcClose::Cancel;
    return RcClose::None;
}

// The adoption of an open target of c as Group (design 7.7): the first check after an entry (adoptDue) counts
// goal_adopt_enter, a later one cross_keep; adoptDue is cleared by the first check in any case. Nothing when the target is not
// an open quest destination of c or Group already holds it.
enum class AdoptKind : std::uint8_t
{
    None,
    Enter,
    Cross,
};

inline AdoptKind AdoptCheck(bool& adoptDue, LiveClass live, bool groupOwns)
{
    bool const due = adoptDue;
    adoptDue = false;
    if (live != LiveClass::QuestGoal || groupOwns)
        return AdoptKind::None;
    return due ? AdoptKind::Enter : AdoptKind::Cross;
}

// ---- move kind (design 7.6, Q8 (a)) -----------------------------------------------------------------------------------------
// b13-14 gatherKind: 0 kind a (follow), 1 kind b (point at the leader's place), 2 kind c (meeting point); 3 is not defined and
// moves as kind a with no ack 3 (an ack 3 would end the group by ack_refused), counted gather_kind3 (T33 design v1.3 Nachtrag 2,
// CLI-GRP 11.11.: the manager never packs kind 3). b15 gatherFlag 0: the leader travels by the quest book to c, the others follow
// the leader (T30, until T33-0).
constexpr std::uint8_t kGatherFollow = 0;
constexpr std::uint8_t kGatherAtLeader = 1;
constexpr std::uint8_t kGatherMeet = 2;
constexpr std::uint8_t kGatherUndefined = 3;

// gather_kind3: a taken Enter or Change (an Exit packs no kind) whose kind is 3, any gather flag. Counted in HandleMail, once per
// directive, superseded or not; the kind still moves as kind a (ChooseMove).
inline bool GatherKindUndefined(VerbClass v, std::uint8_t gatherKind)
{
    return (v == VerbClass::Enter || v == VerbClass::Change) && gatherKind == kGatherUndefined;
}

enum class MoveKind : std::uint8_t
{
    LeaderTravel,
    Follow,
    PointGo,
    PointWait,
};

inline MoveKind ChooseMove(bool gatherFlag, std::uint8_t gatherKind, bool isLeader, bool atPoint = false)
{
    if (gatherFlag && gatherKind == kGatherAtLeader)
        return isLeader ? MoveKind::LeaderTravel : MoveKind::PointGo;
    if (gatherFlag && gatherKind == kGatherMeet)
        return isLeader && atPoint ? MoveKind::PointWait : MoveKind::PointGo;
    return isLeader ? MoveKind::LeaderTravel : MoveKind::Follow;
}

// ---- follower (design 7.6, b15 = 0 and kind a) --------------------------------------------------------------------------------
// The follower check, at most once per second, at Enter and at RESCUE -> GROUP. leaderNear = the leader is found and on the
// bot's own map object (and leaderLow != 0); intact = the follow set still stands. In Follow: complete -> TurnIn once
// (makeTurnIn, follow_turnin; no follower check afterwards); leader far -> no follow set, leader_far only on the near -> far
// edge (wasFar = the last check's leaderFar; countLeaderFar is also true at the first far check after an entry, and the caller
// counts it only once the leader was near in this membership, else follow_self, FollowSelfEdge); near and broken -> makeFollower
// (follow_make).
enum class FollowPhase : std::uint8_t
{
    None,
    Follow,
    TurnIn,
};

struct FollowDecision
{
    bool makeFollower = false;
    bool makeTurnIn = false;
    bool countLeaderFar = false;
    FollowPhase newPhase = FollowPhase::None;
    bool leaderFar = false;    // the new value of the caller's leaderFar
};

inline FollowDecision FollowStep(FollowPhase phase, bool leaderNear, bool intact, bool complete, bool wasFar)
{
    FollowDecision d;
    d.newPhase = phase;
    d.leaderFar = wasFar;
    if (phase != FollowPhase::Follow)
        return d;
    if (complete)
    {
        d.makeTurnIn = true;
        d.newPhase = FollowPhase::TurnIn;
        d.leaderFar = false;
        return d;
    }
    d.leaderFar = !leaderNear;
    if (!leaderNear)
    {
        d.countLeaderFar = !wasFar;
        return d;
    }
    d.makeFollower = !intact;
    return d;
}

// Recheck 11.11. (5): a follower whose leader is 0 or was never near since the entry gets no follow set; it keeps its own target
// and searches the group goal c itself (the snapshot filter holds). Counted follow_self once per membership: everNear = a check
// of this membership found the leader near, counted = follow_self already counted for it.
inline bool FollowSelfEdge(FollowPhase phase, bool leaderNear, bool everNear, bool counted)
{
    return phase == FollowPhase::Follow && !leaderNear && !everNear && !counted;
}

// An accepted move with a follow the handler set: undo it first (as an Exit, without a mode change) when the bot itself leads
// the new group, when the new group has another leader (master and follow set belong to the old one; a far new leader must not
// leave the bot following the old leader, recheck 5), or when the old group's follow phase is over (TurnIn: the new group's
// follower check would never run). The caller's ApplyMove then starts the follow afresh. Same leader, still Follow: it stands.
inline bool MoveUndoFollowFirst(bool roleByHandler, bool newIsLeader, bool sameLeader, bool stillFollow)
{
    return roleByHandler && (newIsLeader || !sameLeader || !stillFollow);
}

// ---- close ring (design 7.7 MAJOR 4) ----------------------------------------------------------------------------------------
// The last kCloseRingSlots closes of goal_close / prep_cancel of one bot. burst: >= 30 in the last 60 s (close_burst, HARM in the
// smoke test), warn: >= 5 in the last 600 s (close_warn, FINDING). Each edge is reported once per crossing; a flag falls back
// when a later push counts fewer than its threshold.
constexpr std::size_t kCloseRingSlots = 32;
constexpr std::uint32_t kCloseBurstCount = 30;
constexpr std::uint32_t kCloseBurstMs = 60000;
constexpr std::uint32_t kCloseWarnCount = 5;
constexpr std::uint32_t kCloseWarnMs = 600000;

struct CloseRing
{
    std::uint32_t atMs[kCloseRingSlots] = {};
    std::uint8_t head = 0;
    std::uint8_t used = 0;
    bool burst = false;
    bool warn = false;
};

struct CloseEdges
{
    bool burstEdge = false;
    bool warnEdge = false;
};

inline CloseEdges ClosePush(std::uint32_t (&atMs)[kCloseRingSlots], std::uint8_t& head, std::uint8_t& used, bool& burst, bool& warn,
    std::uint32_t nowMs)
{
    atMs[head % kCloseRingSlots] = nowMs;
    head = std::uint8_t((head + 1) % kCloseRingSlots);
    if (used < kCloseRingSlots)
        ++used;
    std::uint32_t inBurst = 0;
    std::uint32_t inWarn = 0;
    for (std::size_t i = 0; i < used; ++i)
    {
        std::uint32_t const age = nowMs - atMs[(head + kCloseRingSlots - 1 - i) % kCloseRingSlots];
        if (age < kCloseBurstMs)
            ++inBurst;
        if (age < kCloseWarnMs)
            ++inWarn;
    }
    CloseEdges e;
    bool const burstNow = inBurst >= kCloseBurstCount;
    bool const warnNow = inWarn >= kCloseWarnCount;
    e.burstEdge = burstNow && !burst;
    e.warnEdge = warnNow && !warn;
    burst = burstNow;
    warn = warnNow;
    return e;
}

inline CloseEdges ClosePush(CloseRing& ring, std::uint32_t nowMs)
{
    return ClosePush(ring.atMs, ring.head, ring.used, ring.burst, ring.warn, nowMs);
}

// ---- self exit (design 7.8, 7.9) --------------------------------------------------------------------------------------------
// The lock set with each left ends at an Exit of the same serial (exitSerial; 0 = no Exit in this tick) or kOrphanGraceMs after
// the left, whichever comes first. It does not ask the view. lockSerial 0 = no lock (a group serial is never 0 in its low 16
// bits, NextGroupSerial).
inline bool SelfExitEnds(std::uint16_t lockSerial, std::uint32_t lockSinceMs, std::uint32_t nowMs, std::uint16_t exitSerial)
{
    if (lockSerial == 0)
        return false;
    return exitSerial == lockSerial || std::uint32_t(nowMs - lockSinceMs) >= group_mgr::kOrphanGraceMs;
}

// ---- left retry (design 7.9, m3) ------------------------------------------------------------------------------------------------
// A left whose post failed is posted once more in the next tick (LeftRetryDone: Sent counts left, Dropped counts left_drop); an
// Exit of the same serial in step 4 before it cancels the retry (LeftCancel true: left_cancel). Plain fields: pending = serial != 0.
struct LeftRetry
{
    std::uint16_t serial = 0;
    std::uint8_t reason = 0;
    bool pending = false;
};

enum class LeftStep : std::uint8_t
{
    None,
    Sent,
    Dropped,
};

inline void LeftArm(std::uint16_t& serial, std::uint8_t& reason, std::uint16_t leftSerial, std::uint8_t leftReason)
{
    serial = leftSerial;
    reason = leftReason;
}

inline void LeftArm(LeftRetry& r, std::uint16_t leftSerial, std::uint8_t leftReason)
{
    LeftArm(r.serial, r.reason, leftSerial, leftReason);
    r.pending = leftSerial != 0;
}

inline bool LeftCancel(std::uint16_t& serial, std::uint8_t& reason, std::uint16_t exitSerial)
{
    if (serial == 0 || serial != exitSerial)
        return false;
    serial = 0;
    reason = 0;
    return true;
}

inline bool LeftCancel(LeftRetry& r, std::uint16_t exitSerial)
{
    if (!r.pending)
        return false;
    bool const cancelled = LeftCancel(r.serial, r.reason, exitSerial);
    r.pending = r.serial != 0;
    return cancelled;
}

// posted = the result of the retry post (call only when a retry is pending; None otherwise).
inline LeftStep LeftRetryDone(std::uint16_t& serial, std::uint8_t& reason, bool posted)
{
    if (serial == 0)
        return LeftStep::None;
    serial = 0;
    reason = 0;
    return posted ? LeftStep::Sent : LeftStep::Dropped;
}

inline LeftStep LeftRetryDone(LeftRetry& r, bool posted)
{
    if (!r.pending)
        return LeftStep::None;
    LeftStep const s = LeftRetryDone(r.serial, r.reason, posted);
    r.pending = false;
    return s;
}
}
