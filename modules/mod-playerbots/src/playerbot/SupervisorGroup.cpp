#include "playerbot/playerbot.h"
#include "playerbot/Supervisor.h"
#include "playerbot/SupervisorGroupPolicy.h"
#include "playerbot/SupervisorShadow.h"
#include "playerbot/SupervisorEnforce.h"
#include "playerbot/ManagerInbox.h"
#include "playerbot/GroupManagerView.h"
#include "playerbot/QuestGroupWorld.h"
#include "playerbot/GroupAiRefresh.h"
#include "playerbot/BotSlots.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "Group/Group.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2 and BotGroups.Manager.Types > 0, default 0): the bot side of the group
// manager. Design A3-S3-T02-ENTWURF.md v0.4.3 §7 (build spec A3-S3-T02GROUP-BAUSPEC.md with the recheck of 11.11.). Members of
// Supervisor, bot thread only (the bot's own map update, from Supervisor::Tick / TakeMail / Release); the pure rules are in
// SupervisorGroupPolicy.h (unit test t/supervisor_group_policy_tests.cpp). At the defaults no directive arrives: GroupTick is the
// mailbox registration once per supervisor life, then the early return; HandleMail and GroupRelease return at once.
// The manager is reached only through its two light headers: the ack and the left go through the manager inbox (one call each,
// SendAck / SendLeft), the view question through group_mgr IsManaged with the serial (one call, ManagedNow). The live travel
// target is written in two functions only, each under the Group writer scope: CloseLive (the closers, EXPIRED or NONE) and
// PointWrite (the T33 gather point, one destination write with its result evaluated).
namespace ai::supervisor
{
namespace
{
// Q4 (a), decided by the owner (10.11.2026, relayed by OB-00): the switch point of the handler, true for every bot; T02-GROUP
// and T30 are one lever (Manager.Types at Arbiter 2) with one A/B. Kept as a function: a later cohort rule on
// AiPlayerbot.Supervisor.GroupHandler (owner's word only) would replace the body, and the T30 counterpart (CLI-GRP) would then
// have to end the groups the manager holds for a bot the handler leaves (GroupTick step 3 leaves silently, counted handler_off;
// no left report, Q5 (a)).
bool GroupHandlerOn(std::uint32_t guidLow)
{
    (void)guidLow;
    return true;
}

void CountGrp(std::uint32_t guidLow, GrpField field)
{
    CountEnforce(guidLow, EnforceCount(std::size_t(kGrpFirst) + std::size_t(field)));
}

// The null target, the same rule as the shadow's (SupervisorShadow.cpp IsNullDest).
bool IsNullDest(TravelDestination const* dest)
{
    return !dest || dynamic_cast<NullTravelDestination const*>(dest) != nullptr;
}

// The live travel target of the bot (the value TargetValue.h marks live).
TravelTarget* LiveTarget(PlayerbotAI* ai)
{
    AiObjectContext* const context = ai ? ai->GetAiObjectContext() : nullptr;
    return context ? context->GetValue<TravelTarget*>("travel target")->Get() : nullptr;
}

// R-c class of the live target against the group goal c (SupervisorGroupPolicy.h ClassifyLive).
LiveClass LiveClassOf(TravelTarget* target, std::uint32_t c)
{
    if (!target)
        return LiveClass::None;
    TravelDestination* const dest = target->GetDestination();
    std::uint8_t const status = std::uint8_t(target->GetStatus());
    auto const* quest = dynamic_cast<QuestTravelDestination const*>(dest);
    return ClassifyLive(true, status == kStatusPrepare, IsOpen(status, IsNullDest(dest)), quest != nullptr,
        quest ? quest->GetQuestId() : 0, c);
}

bool InQuestLog(Player* bot, std::uint32_t questId)
{
    return questId != 0 && bot->FindQuestSlot(questId) < MAX_QUEST_LOG_SIZE;
}

// The player focus as one quest id for EnterCheck (4): 0 = no focus, c when c is among the focus quests, else another one.
std::uint32_t FocusQuest(PlayerbotAI* ai, std::uint32_t c)
{
    AiObjectContext* const context = ai->GetAiObjectContext();
    if (!context)
        return 0;
    focusQuestTravelList const focus = context->GetValue<focusQuestTravelList>("focus travel target")->Get();
    if (focus.empty())
        return 0;
    return focus.count(c) ? c : *focus.begin();
}

// R8: the start zone holds the bot and the event's anchor (or the gather point) lies outside its start zone.
bool StartZoneHoldAt(Player* bot, PlayerbotAI* ai, std::uint32_t mapId, float x, float y)
{
    if (!zone_keep::StartZoneHolds(bot, ai))
        return false;
    WorldPosition const anchor(mapId, x, y, bot->GetPositionZ());
    return zone_keep::OutsideStartZone(bot, &anchor) != 0;
}

// The bot stands at the gather point (its map, within kGatherYards): ChooseMove's atPoint, one rule for ApplyMove and the re-apply.
bool AtGatherPoint(Player* bot, std::uint32_t mapId, float x, float y)
{
    float const dx = bot->GetPositionX() - x;
    float const dy = bot->GetPositionY() - y;
    return bot->GetMapId() == mapId && dx * dx + dy * dy <= group_mgr::kGatherYards * group_mgr::kGatherYards;
}

// One [Supervisor] line of this file (the ev=mode line has its own formatter): the common head, then the event text, from the
// shared budget of 300 lines per minute.
void GroupLine(Player* bot, std::uint32_t arbiter, char const* format, ...)
{
    char buf[512];
    std::uint32_t const guid = bot->GetGUIDLow();
    int const head = std::snprintf(buf, sizeof(buf), "[Supervisor] bot=%u name=%.40s cohort=%c arbiter=%u ", guid, bot->GetName(),
        cohort::Letter(guid), arbiter);
    std::size_t const len = Sanitize(buf, head, sizeof(buf));
    va_list args;
    va_start(args, format);
    int const w = std::vsnprintf(buf + len, sizeof(buf) - len, format, args);
    va_end(args);
    Sanitize(buf + len, w, sizeof(buf) - len);
    EmitEnforceLine(guid, buf);
}
}

// R4 (design 7.4): only the newest group directive of the drain counts; it waits in pendingDir_ for GroupTick (same tick). Every
// taken directive counts dir_* by its verb class (an Enter or Change of gather kind 3 also gather_kind3), each older one (and a
// still pending one it replaces) superseded. No write here.
void Supervisor::HandleMail(Event const* mail, std::size_t n)
{
    std::size_t superseded = 0;
    std::size_t const newest = NewestDirective(mail, n, superseded);
    if (newest >= n)
        return;
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    std::uint32_t const guidLow = bot ? bot->GetGUIDLow() : 0;
    for (std::size_t i = 0; i < n; ++i)
        if (mail[i].kind == EventKind::GroupDirective)
        {
            group_mgr::DirectiveBits const d = group_mgr::UnpackDirective(mail[i].b);
            VerbClass const v = ClassifyVerb(d.verb);
            CountGrp(guidLow, DirField(v));
            if (GatherKindUndefined(v, d.gatherKind))
                CountGrp(guidLow, GrpField::GatherKind3);
        }
    for (std::size_t i = 0; i < superseded; ++i)
        CountGrp(guidLow, GrpField::Superseded);
    if (hasPendingDir_)
        CountGrp(guidLow, GrpField::Superseded);
    pendingDir_ = mail[newest];
    hasPendingDir_ = true;
}

// Design 7.4, once per engine pass right after the shadow hand-over. Order: 0 the RESCUE flag, 1 the registration, 2 the owner
// sample, 8 the early return, 3 the switch, 4 the directive, 5 the left retry, 7 death and revival, 6 the GROUP checks. Step 7
// runs before step 6: a bot that died in GROUP leaves it in this tick, so the once-per-second checks never read a dead bot's quest
// log or follower state.
void Supervisor::GroupTick(std::uint32_t nowMs, std::uint32_t arbiter)
{
    if (mode_ != Mode::Rescue)
        rescueByGroup_ = false;
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    std::uint32_t const guidLow = bot->GetGUIDLow();
    groupArbiter_ = arbiter;

    // 1. R10: one registration per supervisor life (the mailbox lock once); ~Supervisor unregisters it by token.
    if (!mailGuid_)
    {
        mailGuid_ = guidLow;
        mailToken_ = RegisterMail(guidLow);
    }

    // 2. The owner sample (m7): at arbiter 2 once per bot and game minute, before any early return, without moving the owner.
    if (arbiter == 2 && nextSampleMin_ != nowMs / 60000u + 1u)
    {
        nextSampleMin_ = nowMs / 60000u + 1u;
        TravelTarget* const target = LiveTarget(ai_);
        Lane const held = PeekOwner(target);
        if (held == Lane::Questbook)
            CountGrp(guidLow, GrpField::OwnerQ);
        else if (held == Lane::Group)
            CountGrp(guidLow, GrpField::OwnerG);
        if (mode_ == Mode::Group && LiveClassOf(target, groupQuest_) == LiveClass::NonQuest)
            CountGrp(guidLow, GrpField::Nonquest);
    }

    // 8. Nothing to do outside GROUP and RESCUE without a held group, a pending directive, a left retry or a self-exit lock.
    if (mode_ != Mode::Group && mode_ != Mode::Rescue && !groupHeld_ && !hasPendingDir_ && !leftRetrySerial_ && !selfExitSerial_)
        return;

    // 3. Arbiter != 2 (the one tick of a switch away from 2) or the handler off (Q4 (b)/(c) only): leave a held group silently.
    HandlerLeave const leave = HandlerLeaveCause(arbiter == 2, GroupHandlerOn(guidLow), groupHeld_);
    if (leave != HandlerLeave::None)
    {
        if (leave == HandlerLeave::HandlerOff)
            CountGrp(guidLow, GrpField::HandlerOff);
        bool const inGroup = mode_ == Mode::Group;
        LeaveGroupMode(leave == HandlerLeave::HandlerOff ? "handler_off" : "arbiter_off");
        if (inGroup)
            GroupToQuest(Requester::Self, leave == HandlerLeave::HandlerOff ? "handler_off" : "arbiter_off");
    }

    // 4. The pending directive: exactly one ack. An Exit hands its serial to the self-exit lock below.
    std::uint16_t exitSerial = 0;
    if (hasPendingDir_)
    {
        Event const e = pendingDir_;
        hasPendingDir_ = false;
        exitSerial = AnswerDirective(e, arbiter);
    }
    if (SelfExitEnds(selfExitSerial_, selfExitSinceMs_, nowMs, exitSerial))
    {
        selfExitSerial_ = 0;
        selfExitSinceMs_ = 0;
    }

    // 5. The left of the tick before whose post failed (and that no Exit of its serial cancelled): posted once more.
    if (leftRetrySerial_)
        SendLeft(leftRetrySerial_, leftRetryReason_, true);

    // Below arbiter 2 nothing else runs (no group is held after step 3; the disarming edge follows in this tick).
    if (arbiter != 2)
        return;

    // 7. Death in GROUP: the RESCUE funnel. Role, master and the follower set stay while dead (S7 parity).
    if (mode_ == Mode::Group && !bot->IsAlive())
    {
        TransitionResult const result = Decide(Mode::Group, Mode::Rescue, Requester::Self, phase_, 0, Cadence{});
        CommitMode(result, Mode::Group, Mode::Rescue, Requester::Self, "group_death");
        if (result.allowed)
        {
            OrphanReset(orphanFalseSinceMs_, orphanNextAskMs_);
            rescueByGroup_ = true;
            CountGrp(guidLow, GrpField::RescueIn);
        }
        return;
    }
    if (mode_ == Mode::Rescue)
    {
        if (rescueByGroup_ && bot->IsAlive())
        {
            // Alive again in this handler's RESCUE: with a held group the view is asked at once (fresh, design 7.5).
            bool const managed = groupHeld_ && ManagedNow();
            RescueEnd const end = RescueReturn(rescueByGroup_, true, groupHeld_, managed);
            if (end == RescueEnd::ToGroup)
            {
                TransitionResult const result = Decide(Mode::Rescue, Mode::Group, Requester::Self, phase_, 0, Cadence{});
                CommitMode(result, Mode::Rescue, Mode::Group, Requester::Self, "rescue_group");
                if (result.allowed)
                {
                    rescueByGroup_ = false;
                    // The fresh question above is this window's ask: the next one in 10 s (design 7.9), not in the next pass.
                    OrphanResetAsked(orphanFalseSinceMs_, orphanNextAskMs_, nowMs);
                    ApplyGoalRule(LiveTarget(ai_));   // R-c as at Enter, adoptDue_
                    ApplyMove();                      // a follower is checked at once
                    CountGrp(guidLow, GrpField::RescueGroup);
                }
                return;
            }
            if (end == RescueEnd::ToQuestLeave)
                LeaveGroupMode("rescue_unmanaged");
            rescueByGroup_ = false;
            TransitionResult const result = Decide(Mode::Rescue, Mode::Quest, Requester::Self, phase_, 0, Cadence{});
            CommitMode(result, Mode::Rescue, Mode::Quest, Requester::Self,
                end == RescueEnd::ToQuestLeave ? "rescue_unmanaged" : "rescue_end");
            CountGrp(guidLow, GrpField::RescueBack);
            return;
        }
        // Dead (or a RESCUE of another module) with a held group: only the orphan rule, RESCUE branch of the leave (no Decide).
        if (groupHeld_ && OrphanAskTake(orphanNextAskMs_, nowMs) && OrphanStep(orphanFalseSinceMs_, nowMs, ManagedNow()))
        {
            CountGrp(guidLow, GrpField::Orphan);
            GroupLine(bot, arbiter, "ev=group_orphan serial=%u quest=%u in=RESCUE", unsigned(groupSerial_), groupQuest_);
            LeaveGroupMode("group_orphan");
        }
        return;
    }
    if (mode_ != Mode::Group || !groupHeld_)
        return;

    // 6. In GROUP. The orphan rule (one view question per 10 s at most).
    if (OrphanAskTake(orphanNextAskMs_, nowMs) && OrphanStep(orphanFalseSinceMs_, nowMs, ManagedNow()))
    {
        CountGrp(guidLow, GrpField::Orphan);
        GroupLine(bot, arbiter, "ev=group_orphan serial=%u quest=%u in=GROUP", unsigned(groupSerial_), groupQuest_);
        LeaveGroupMode("group_orphan");
        GroupToQuest(Requester::Self, "group_orphan");
        return;
    }

    // Goal end, R-c goal_close, the follower check and the adoption: together at most once per second.
    if (!PeriodDue(nextSecondMs_, nowMs, kGroupCheckMs))
        return;
    GoalEnd const end = ClassifyGoal(quest_groups::TurnedIn(bot, groupQuest_), InQuestLog(bot, groupQuest_));
    if (end != GoalEnd::None)
    {
        // Order (m3): leave and Decide first, then the left report, then the self-exit lock.
        std::uint16_t const serial = groupSerial_;
        std::uint8_t const why = GoalEndReason(end);
        CountGrp(guidLow, end == GoalEnd::Done ? GrpField::GoalDone : GrpField::GoalLost);
        GroupLine(bot, arbiter, "ev=group_goal_end serial=%u quest=%u end=%s", unsigned(serial), groupQuest_,
            end == GoalEnd::Done ? "goal_done" : "goal_lost");
        LeaveGroupMode(end == GoalEnd::Done ? "goal_done" : "goal_lost");
        GroupToQuest(Requester::Self, end == GoalEnd::Done ? "goal_done" : "goal_lost");
        SendLeft(serial, why, false);
        selfExitSerial_ = serial;
        selfExitSinceMs_ = nowMs;
        return;
    }
    TravelTarget* const target = LiveTarget(ai_);
    if (RcCheck(LiveClassOf(target, groupQuest_), false) == RcClose::Expire)
        CloseLive(target, kStatusExpired, "group_goal", true);
    if (roleByHandler_)
        FollowCheck();
    LiveClass const live = LiveClassOf(target, groupQuest_);
    AdoptKind const adopt = AdoptCheck(adoptDue_, live, PeekOwner(target) == Lane::Group);
    if (adopt != AdoptKind::None)
        AdoptGroupOwner(target->WriteGen(), adopt == AdoptKind::Enter ? EnforceCount::GoalAdoptEnter : EnforceCount::CrossKeep);
    bool const isLeader = leaderLow_ != 0 && leaderLow_ == guidLow;
    if (!leadBegun_ && isLeader && live == LiveClass::QuestGoal)
    {
        leadBegun_ = true;
        GroupLine(bot, arbiter, "ev=lead_begin serial=%u quest=%u", unsigned(groupSerial_), groupQuest_);
    }
    // The gather point (flag 1, kind b/c) is applied again when its temporary target ran out, while done = 0. atPoint as in
    // ApplyMove: a kind-c leader waiting at the point (PointWait) gets no new write.
    if (!gatherDone_ && target && bot->GetMapId() == groupMap_ &&
        ChooseMove(gatherFlag_, gatherKind_, isLeader, AtGatherPoint(bot, groupMap_, groupX_, groupY_)) == MoveKind::PointGo &&
        !IsOpen(std::uint8_t(target->GetStatus()), IsNullDest(target->GetDestination())) && PointWrite())
        CountGrp(guidLow, GrpField::PointReapply);
}

// Design 7.5: one directive, one decision, one ack. Returns the serial of an Exit (0 for any other verb) for the self-exit lock.
std::uint16_t Supervisor::AnswerDirective(Event const& e, std::uint32_t arbiter)
{
    Player* const bot = ai_->GetBot();
    std::uint32_t const guidLow = bot->GetGUIDLow();
    group_mgr::DirectiveBits const d = group_mgr::UnpackDirective(e.b);
    VerbClass const verb = ClassifyVerb(d.verb);
    AckDecision decided = AckOk();
    ExitClass exitKind = ExitClass::ExitStale;
    std::uint16_t exitSerial = 0;
    switch (verb)
    {
        case VerbClass::Enter:
        case VerbClass::Change:
        {
            bool const handlerOn = GroupHandlerOn(guidLow);
            if (verb == VerbClass::Enter)
                EnterGroup(e, arbiter == 2, handlerOn, decided.state, decided.reason);
            else
                ChangeGroup(e, arbiter == 2, handlerOn, decided.state, decided.reason);
            break;
        }
        case VerbClass::Exit:
        {
            bool const same = groupHeld_ && d.serial == groupSerial_;
            ExitDecision const x = ExitCheck(mode_ == Mode::Group && groupHeld_, mode_ == Mode::Rescue && groupHeld_, same);
            decided = x.ack;
            exitKind = x.kind;
            exitSerial = d.serial;
            if (x.kind == ExitClass::ExitApplied)
            {
                LeaveGroupMode("exit");
                TransitionResult const result = Decide(Mode::Group, Mode::Quest, Requester::GroupMgr, phase_, 0, Cadence{});
                CommitMode(result, Mode::Group, Mode::Quest, Requester::GroupMgr, "exit");
            }
            else if (x.kind == ExitClass::ExitInRescue)
                LeaveGroupMode("exit");
            if (LeftCancel(leftRetrySerial_, leftRetryReason_, d.serial))
                CountGrp(guidLow, GrpField::LeftCancel);
            break;
        }
        case VerbClass::Bad:
            decided = BadVerbAck();
            break;
    }
    CountGrp(guidLow, AckOutcomeField(verb, decided, exitKind));
    GrpField reasonField = GrpField::RDeny;
    if (ReasonField(decided.reason, reasonField))
        CountGrp(guidLow, reasonField);
    SendAck(e, decided.state, decided.reason);
    return exitSerial;
}

// Enter (design 7.5): the checks (SupervisorGroupPolicy.h EnterCheck), then outside GROUP Decide; a move (GROUP, another serial)
// takes the new group without Decide and leaves GROUP when refused. Write first, then commit GROUP: R-c and the move run before
// the mode changes; a failed own write rolls back and answers 3/23 lane_busy.
void Supervisor::EnterGroup(Event const& e, bool arbiter2, bool handlerOn, std::uint8_t& state, std::uint8_t& reason)
{
    Player* const bot = ai_->GetBot();
    group_mgr::DirectiveBits const d = group_mgr::UnpackDirective(e.b);
    EnterFacts f;
    f.arbiter2 = arbiter2;
    f.handlerOn = handlerOn;
    f.type = std::uint8_t(d.type);
    f.inGroup = mode_ == Mode::Group && groupHeld_;
    f.sameSerial = groupHeld_ && d.serial == groupSerial_;
    f.inRescue = mode_ == Mode::Rescue;
    // (1) the bot's own role (recheck 4): a role the handler did not set is S7's or BOT-12's.
    f.roleNotByHandler = ai_->GetQuestGroupRole() != 0 && !roleByHandler_;
    f.c = e.c;
    if (f.arbiter2 && f.handlerOn && !f.inRescue && !(f.inGroup && f.sameSerial))
    {
        f.startZoneHold = StartZoneHoldAt(bot, ai_, e.mapId, e.x, e.y);
        f.goalInLog = InQuestLog(bot, e.c);
        f.goalDone = e.c != 0 && quest_groups::Complete(bot, e.c);
        f.goalTurnedIn = e.c != 0 && quest_groups::TurnedIn(bot, e.c);
        f.focusQuest = FocusQuest(ai_, e.c);
    }
    AckDecision const checked = EnterCheck(f);
    bool const move = EnterIsMove(f);
    if (!AckApplied(checked))
    {
        if (move)
        {
            LeaveGroupMode("move_refused");
            GroupToQuest(Requester::Self, "move_refused");
        }
        state = checked.state;
        reason = checked.reason;
        return;
    }
    state = checked.state;
    reason = checked.reason;
    if (EnterIdempotent(f))
        return;

    Mode const from = mode_;
    TransitionResult result = Allow(Mode::Group);
    if (!move)
    {
        result = Decide(from, Mode::Group, Requester::GroupMgr, phase_, 0, Cadence{});
        AckDecision const decided = AckFromDecide(result);
        if (!AckApplied(decided))
        {
            GroupModeLine(from, Mode::Group, result, Requester::GroupMgr, "enter");
            state = decided.state;
            reason = decided.reason;
            return;
        }
    }

    // The new group. The leader is the core group's leader now (re-read only here, on an accepted Enter or move).
    Group* const coreGroup = bot->GetGroup();
    std::uint32_t const newLeader = coreGroup ? std::uint32_t(coreGroup->GetLeaderGuid().GetCounter()) : 0;
    // A move: the old group's follow (master, set, phase) goes first unless the same leader is still followed; ApplyMove below
    // starts it afresh for the new group (leaderLow_ is still the old leader here).
    if (move && MoveUndoFollowFirst(roleByHandler_, newLeader == bot->GetGUIDLow(), newLeader == leaderLow_,
            FollowPhase(followPhase_) == FollowPhase::Follow))
        UndoFollow();
    groupSerial_ = d.serial;
    groupSeq_ = e.a;
    groupType_ = std::uint8_t(d.type);
    groupQuest_ = e.c;
    groupObjective_ = d.objective;
    groupMap_ = e.mapId;
    groupX_ = e.x;
    groupY_ = e.y;
    gatherKind_ = d.gatherKind;
    gatherFlag_ = d.gatherFlag;
    gatherDone_ = false;
    leaderLow_ = newLeader;
    leaderFar_ = false;
    leaderSeen_ = false;
    followSelf_ = false;
    leadBegun_ = false;
    ApplyGoalRule(LiveTarget(ai_));
    if (!ApplyMove())
    {
        // Roll back: a fresh Enter keeps its mode and forgets the group; a move has lost its old group already (the manager
        // ended it before the move), so it leaves GROUP.
        if (move)
        {
            LeaveGroupMode("lane_busy");
            GroupToQuest(Requester::Self, "lane_busy");
        }
        else
        {
            if (roleByHandler_)
                UndoFollow();
            ForgetGroup();
        }
        AckDecision const busy = AckNo(group_mgr::kAckReasonLaneBusy);
        state = busy.state;
        reason = busy.reason;
        return;
    }
    groupHeld_ = true;
    OrphanReset(orphanFalseSinceMs_, orphanNextAskMs_);
    if (!move)
        CommitMode(result, from, Mode::Group, Requester::GroupMgr, "enter");
    else
        GroupLine(bot, groupArbiter_, "ev=group_move serial=%u quest=%u leader=%u", unsigned(groupSerial_), groupQuest_, leaderLow_);
}

// Update (the T33 verb; the manager sends none before T33-0): the gather kind and flag of the held group, and the gather point.
// done = 1 is read as the flag going from 1 to 0 on an Update (UNVERIFIED against T33-0, CLI-GRP owns the values): from then on
// every member travels by the quest book to c (UndoFollow in ApplyMove).
void Supervisor::ChangeGroup(Event const& e, bool arbiter2, bool handlerOn, std::uint8_t& state, std::uint8_t& reason)
{
    Player* const bot = ai_->GetBot();
    group_mgr::DirectiveBits const d = group_mgr::UnpackDirective(e.b);
    bool const point = d.gatherFlag && (d.gatherKind == kGatherAtLeader || d.gatherKind == kGatherMeet);
    ChangeFacts f;
    f.arbiter2 = arbiter2;
    f.handlerOn = handlerOn;
    f.inGroup = mode_ == Mode::Group && groupHeld_;
    f.sameSerial = groupHeld_ && d.serial == groupSerial_;
    f.pointOtherMap = point && e.mapId != bot->GetMapId();
    f.pointStartZoneHold = point && !f.pointOtherMap && StartZoneHoldAt(bot, ai_, e.mapId, e.x, e.y);
    AckDecision const checked = ChangeCheck(f);
    state = checked.state;
    reason = checked.reason;
    if (!AckApplied(checked))
        return;
    std::uint8_t const oldKind = gatherKind_;
    bool const oldFlag = gatherFlag_;
    bool const oldDone = gatherDone_;
    std::uint32_t const oldMap = groupMap_;
    float const oldX = groupX_;
    float const oldY = groupY_;
    if (gatherFlag_ && !d.gatherFlag)
        gatherDone_ = true;
    gatherKind_ = d.gatherKind;
    gatherFlag_ = d.gatherFlag;
    if (point)
    {
        groupMap_ = e.mapId;
        groupX_ = e.x;
        groupY_ = e.y;
    }
    if (ApplyMove())
        return;
    gatherKind_ = oldKind;
    gatherFlag_ = oldFlag;
    gatherDone_ = oldDone;
    groupMap_ = oldMap;
    groupX_ = oldX;
    groupY_ = oldY;
    AckDecision const busy = AckNo(group_mgr::kAckReasonLaneBusy);
    state = busy.state;
    reason = busy.reason;
}

// The one ack of a directive (state and reason from the pure group headers, never a number here). A failed post counts
// ack_drop_enter / ack_drop_exit and is not repeated: the ack counts as answered (design 7.4 step 5).
void Supervisor::SendAck(Event const& e, std::uint8_t state, std::uint8_t reason)
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    std::uint32_t const guidLow = bot->GetGUIDLow();
    group_mgr::DirectiveBits const d = group_mgr::UnpackDirective(e.b);
    bool const posted = ai::manager_inbox::PostDirectiveAck(guidLow, e.a, state, reason);
    if (!posted)
        CountGrp(guidLow, AckDropField(ClassifyVerb(d.verb)));
    GroupLine(bot, groupArbiter_, "ev=directive verb=%s serial=%u seq=%u type=%u quest=%u obj=%d ack=%u reason=%s posted=%u",
        group_mgr::VerbName(d.verb), unsigned(d.serial), e.a, unsigned(d.type), e.c, int(d.objective), unsigned(state),
        group_mgr::AckReasonName(reason), posted ? 1u : 0u);
}

// The bot left GROUP itself (goal_done / goal_lost only, Q5 (a)). serial is the group the bot held (LeaveGroupMode already forgot
// it). A failed first post arms the one retry of the next tick (left_drop when that fails too); retry = that second post.
void Supervisor::SendLeft(std::uint16_t serial, std::uint8_t reason, bool retry)
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    std::uint32_t const guidLow = bot->GetGUIDLow();
    bool const posted = ai::manager_inbox::PostDirectiveLeft(guidLow, serial, reason);
    if (retry)
    {
        LeftStep const step = LeftRetryDone(leftRetrySerial_, leftRetryReason_, posted);
        if (step == LeftStep::Sent)
            CountGrp(guidLow, GrpField::Left);
        else if (step == LeftStep::Dropped)
            CountGrp(guidLow, GrpField::LeftDrop);
    }
    else if (posted)
        CountGrp(guidLow, GrpField::Left);
    else
        LeftArm(leftRetrySerial_, leftRetryReason_, serial, reason);
    GroupLine(bot, groupArbiter_, "ev=left reason=%s serial=%u posted=%u retry=%u", group_mgr::AckReasonName(reason),
        unsigned(serial), posted ? 1u : 0u, retry ? 1u : 0u);
}

// The only closer of this file. EXPIRED or NONE as two literal branches (never a status parameter on the write). loop = R-c or the
// once-per-second goal_close, and the follower's own target at MakeFollower: counted (goal_close for EXPIRED, prep_cancel for
// NONE), fed into the close ring (close_burst / close_warn edges) and written as ev=goal_close / ev=prep_cancel with the writer
// of the closed generation (off = a foreign or off-thread write). Without loop (group_end) only an ev=group_close line.
void Supervisor::CloseLive(ai::TravelTarget* target, std::uint8_t status, char const* cause, bool loop)
{
    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Group);
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot || !target)
        return;
    std::uint32_t const guidLow = bot->GetGUIDLow();
    std::uint32_t const gen = target->WriteGen();
    auto const* quest = dynamic_cast<QuestTravelDestination const*>(target->GetDestination());
    std::uint32_t const questId = quest ? quest->GetQuestId() : 0;
    char const* const writer = lastWriterGen_ == gen ? WriterName(lastWriter_) : "off";
    if (status == kStatusExpired)
        target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
    else
        target->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
    char const* ev = "ev=group_close";
    if (loop)
    {
        bool const expire = status == kStatusExpired;
        ev = expire ? "ev=goal_close" : "ev=prep_cancel";
        CountGrp(guidLow, expire ? GrpField::GoalClose : GrpField::PrepCancel);
        CloseEdges const edges = ClosePush(closeAtMs_, closeHead_, closeUsed_, closeBurst_, closeWarn_, WorldTimer::getMSTime());
        if (edges.burstEdge)
            CountGrp(guidLow, GrpField::CloseBurst);
        if (edges.warnEdge)
            CountGrp(guidLow, GrpField::CloseWarn);
    }
    GroupLine(bot, groupArbiter_, "%s cause=%s writer=%s gen=%u quest=%u c=%u", ev, cause ? cause : "-", writer, gen, questId,
        groupQuest_);
}

// The view question (Q9 (a)): the registry holds the bot as a live member of the held group. Callers ask at most once per 10 s
// (OrphanAskTake), except the fresh first question after a revival.
bool Supervisor::ManagedNow()
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    return bot && groupHeld_ && ai::group_mgr::IsManaged(bot->GetGUIDLow(), groupSerial_);
}

// Design 7.6, the move of an accepted Enter, move, Update and of RESCUE -> GROUP (its three callers; the contract pins them).
// Leader (flag 0, kind a) and every member after done = 1: the quest book travels to c (no write here). Follower: StartFollow.
// Gather point (flag 1, kind b/c): PointWrite on the bot's own map; a point on another map holds (an Update refuses it with 3/19
// before). false = the own write was denied: then nothing else changed here (the follow is undone only after the write held), so
// the caller's roll back restores a consistent state (an Update keeps its follower).
bool Supervisor::ApplyMove()
{
    Player* const bot = ai_->GetBot();
    bool const isLeader = leaderLow_ != 0 && leaderLow_ == bot->GetGUIDLow();
    bool const atPoint = AtGatherPoint(bot, groupMap_, groupX_, groupY_);
    MoveKind const kind = gatherDone_ ? MoveKind::LeaderTravel : ChooseMove(gatherFlag_, gatherKind_, isLeader, atPoint);
    if (kind == MoveKind::Follow)
    {
        StartFollow();
        return true;
    }
    if (kind == MoveKind::PointGo && bot->GetMapId() == groupMap_ && !PointWrite())
        return false;
    // The done = 1 teardown and every non-follow move: the follow set goes (after the point write held).
    if (roleByHandler_)
        UndoFollow();
    return true;
}

// The gather point as a temporary destination (the target frees it when replaced; a denied write frees it in SetTarget's gate).
// Height from the bot's own map object at the point, else the bot's own height.
bool Supervisor::PointWrite()
{
    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Group);
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    TravelTarget* const target = LiveTarget(ai_);
    if (!bot || !target)
        return false;
    float z = bot->GetPositionZ();
    if (Map* const map = bot->GetMap())
    {
        float const ground = map->GetHeight(groupX_, groupY_, z);
        if (ground > INVALID_HEIGHT)
            z = ground;
    }
    TemporaryTravelDestination* const dest = new TemporaryTravelDestination(WorldPosition(groupMap_, groupX_, groupY_, z));
    if (!target->SetTarget(dest, dest->GetPosition()))
        return false;
    return true;
}

// R-c at Enter, at a move and at RESCUE -> GROUP (design 7.7): an open quest target of another quest expires (group_goal), a
// PREPARE is cancelled (group_prepare; a search started before the group has the old snapshot); the travel destinations are
// recomputed; the first check in GROUP adopts an open target of c (adoptDue_).
void Supervisor::ApplyGoalRule(ai::TravelTarget* target)
{
    if (AiObjectContext* const context = ai_->GetAiObjectContext())
        context->ClearValues("no active travel destinations");
    adoptDue_ = true;
    RcClose const close = RcCheck(LiveClassOf(target, groupQuest_), true);
    if (close == RcClose::Expire)
        CloseLive(target, kStatusExpired, "group_goal", true);
    else if (close == RcClose::Cancel)
        CloseLive(target, kStatusNone, "group_prepare", true);
}

// A follower of the held group (flag 0 or kind a): role 1 by the handler, then the follower check at once. Called only from
// ApplyMove (contract), so at Enter / move / RESCUE -> GROUP, and from an Update (ChangeGroup, the T33 verb: the manager sends
// none before T33-0) that turns a gather kind b/c or a flag 1 into a follow. A role already set by the handler keeps its phase
// (a TurnIn survives RESCUE -> GROUP and an Update); a move resets it first (MoveUndoFollowFirst).
void Supervisor::StartFollow()
{
    if (!roleByHandler_)
    {
        ai_->SetQuestGroupRole(1);
        roleByHandler_ = true;
        followPhase_ = std::uint8_t(FollowPhase::Follow);
        leaderFar_ = false;
        leaderSeen_ = false;
        followSelf_ = false;
    }
    FollowCheck();
}

// The follower check (design 7.6, recheck 4/5), at most once per second in GROUP, and at once from StartFollow. Guard first: the
// leader's player is read only when it stands on this bot's map object (SameMapObject), then FollowerIntact, then MakeFollower
// only when the follow set is broken. complete -> the turn-in set once. Leader 0 or never near since the entry: no follow set,
// the bot keeps its own target and searches c itself (follow_self, once per membership); a leader that was near and is far now
// counts leader_far once per edge.
// Recheck 1: AiPlayerbot.BotGroups.LeaderOtherMap (default 0) is not changed here: at 0 the follow action sends a cross-map route
// to a leader on another map in each update; such a leader is not on this map object, so it counts as leader_far. The R9 PREREG
// pins the key.
// Recheck 2, the follower's own target from before the Enter: closed, not saved and restored. Restoring like S7 would write as
// Select under the group's own owner (a gate denial) and, after done = 1 or a move, would bring back a pre-group target where R-c
// wants the quest book's search for c; and a saved target (strings, pointers) is no plain field. So when MakeFollower runs, an
// open live target that is not a quest destination (R-c already closed other quests and PREPARE) is closed through CloseLive
// (EXPIRED, cause follow_start), counted goal_close and fed into the close ring like every R-c close.
void Supervisor::FollowCheck()
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot || !roleByHandler_ || FollowPhase(followPhase_) != FollowPhase::Follow)
        return;
    std::uint32_t const guidLow = bot->GetGUIDLow();
    Player* const leader = leaderLow_ ? sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, leaderLow_)) : nullptr;
    bool const leaderNear = leader && SameMapObject(bot, leader);
    bool const intact = leaderNear && quest_groups::FollowerIntact(ai_, leader);
    FollowDecision const step =
        FollowStep(FollowPhase::Follow, leaderNear, intact, quest_groups::Complete(bot, groupQuest_), leaderFar_);
    followPhase_ = std::uint8_t(step.newPhase);
    leaderFar_ = step.leaderFar;
    if (FollowSelfEdge(step.newPhase, leaderNear, leaderSeen_, followSelf_))
    {
        followSelf_ = true;
        CountGrp(guidLow, GrpField::FollowSelf);
    }
    if (step.countLeaderFar && leaderSeen_)
        CountGrp(guidLow, GrpField::LeaderFar);
    if (leaderNear)
        leaderSeen_ = true;
    if (step.makeTurnIn)
    {
        quest_groups::MakeTurnIn(ai_);
        CountGrp(guidLow, GrpField::FollowTurnin);
        return;
    }
    if (step.makeFollower && leader && SameMapObject(bot, leader) && !quest_groups::FollowerIntact(ai_, leader))
    {
        quest_groups::MakeFollower(ai_, leader);
        CountGrp(guidLow, GrpField::FollowMake);
        TravelTarget* const target = LiveTarget(ai_);
        if (LiveClassOf(target, groupQuest_) == LiveClass::NonQuest)
            CloseLive(target, kStatusExpired, "follow_start", true);
    }
}

// Undo the follow (Exit and every other leave, the done = 1 teardown, a move follower -> leader): role 0, no master, the leave
// reset posted to the bot's own next UpdateAI (it drops a pending follow or turn-in set, GroupAiRefresh.h Merge).
void Supervisor::UndoFollow()
{
    ai_->SetQuestGroupRole(0);
    ai_->SetMaster(nullptr);
    ai_->RequestGroupAi(group_ai_refresh::LeaveReset);
    roleByHandler_ = false;
    followPhase_ = std::uint8_t(FollowPhase::None);
    leaderFar_ = false;
    leaderSeen_ = false;
    followSelf_ = false;
}

// Design 7.10, the one full reset (bot thread only; never from ~Supervisor). In GROUP the group's own open target is closed
// (group_end) and the caller's Decide writes the ev=mode line; the RESCUE branch (no close, no Decide: the live target belongs to
// the survival) writes its own ev=mode line with the mode unchanged.
void Supervisor::LeaveGroupMode(char const* cause)
{
    bool const inGroup = mode_ == Mode::Group;
    if (AiObjectContext* const context = ai_ ? ai_->GetAiObjectContext() : nullptr)
        context->ClearValues("no active travel destinations");
    if (roleByHandler_)
        UndoFollow();
    if (inGroup)
    {
        TravelTarget* const target = LiveTarget(ai_);
        if (target && PeekOwner(target) == Lane::Group)
            CloseLive(target, kStatusExpired, "group_end", false);
    }
    SetOwner(Lane::None, 0);
    ForgetGroup();
    if (!inGroup)
        GroupModeLine(mode_, mode_, Allow(mode_), Requester::Self, cause);
}

// Release (first statement): only with a held group. No left report: the manager ends the group itself (no_supervisor).
void Supervisor::GroupRelease()
{
    if (!groupHeld_)
        return;
    bool const inGroup = mode_ == Mode::Group;
    LeaveGroupMode("release");
    if (inGroup)
        GroupToQuest(Requester::Self, "release");
}

// HeldOwner without clearing a stale owner (the owner sample, the adoption check, the group_end close).
Lane Supervisor::PeekOwner(ai::TravelTarget* target) const
{
    Lane const held = owner_;
    if (held == Lane::None || !target)
        return Lane::None;
    bool const holds = OwnerHolds(held, ownerGen_, target->WriteGen(), std::uint8_t(target->GetStatus()),
        IsNullDest(target->GetDestination()), mode_ == Mode::Group || inProbe_);
    return holds ? held : Lane::None;
}

// The one group lane owner write (design 7.7): goal_adopt_enter / cross_keep from GroupTick, goal_adopt_write from
// CommitTargetWrite (SupervisorEnforce.cpp).
void Supervisor::AdoptGroupOwner(std::uint32_t gen, EnforceCount why)
{
    SetOwner(Lane::Group, gen);
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (bot)
        CountEnforce(bot->GetGUIDLow(), why);
}

// The held group is forgotten (the follow state is UndoFollow's).
void Supervisor::ForgetGroup()
{
    groupHeld_ = false;
    groupSerial_ = 0;
    groupSeq_ = 0;
    groupType_ = 0;
    groupQuest_ = 0;
    groupObjective_ = -1;
    groupMap_ = 0;
    groupX_ = 0.0f;
    groupY_ = 0.0f;
    gatherKind_ = 0;
    gatherFlag_ = false;
    gatherDone_ = false;
    leaderLow_ = 0;
    leadBegun_ = false;
    adoptDue_ = false;
    nextSecondMs_ = 0;
    OrphanReset(orphanFalseSinceMs_, orphanNextAskMs_);
}

// GROUP -> QUEST by Self or the manager (the leave already ran); nothing outside GROUP.
void Supervisor::GroupToQuest(Requester who, char const* cause)
{
    if (mode_ != Mode::Group)
        return;
    TransitionResult const result = Decide(Mode::Group, Mode::Quest, who, phase_, 0, Cadence{});
    CommitMode(result, Mode::Group, Mode::Quest, who, cause);
}

void Supervisor::CommitMode(TransitionResult const& result, Mode from, Mode to, Requester who, char const* cause)
{
    if (result.allowed)
        mode_ = result.to;
    GroupModeLine(from, to, result, who, cause);
}

// One ev=mode line (FormatModeLine) of the handler, with its requester, from the shared line budget.
void Supervisor::GroupModeLine(Mode from, Mode to, TransitionResult const& result, Requester who, char const* cause)
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    ModeLine line;
    line.guid = bot->GetGUIDLow();
    line.name = bot->GetName();
    line.cohort = cohort::Letter(line.guid);
    line.arbiter = groupArbiter_;
    line.level = bot->GetLevel();
    line.map = bot->GetMapId();
    line.zone = bot->GetZoneId();
    line.cause = cause;
    line.from = from;
    line.to = to;
    line.phase = phase_;
    line.req = who;
    line.deny = result.allowed ? Deny::None : result.reason;
    char buf[512];
    FormatModeLine(buf, sizeof(buf), line);
    EmitEnforceLine(line.guid, buf);
}
}
