// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0): the pure rules of the bot
// side of the group manager (SupervisorGroupPolicy.h; design A3-S3-T02-ENTWURF.md v0.4.3 section 7, items of 7.14 item 4, build
// spec A3-S3-T02GROUP-BAUSPEC.md 2.2 with the recheck of 11.11.). The header names no game type, so this suite compiles it
// standalone. The game side (SupervisorGroup.cpp: the one ack, left and view call, the writes, the follower) is pinned by
// t/contracts.d/a3s3_supervisor_group_source_contract_tests.cmake. The Bot struct below is a caller model of GroupTick and
// HandleMail built only from the pure functions (design 7.4 order); it is not the game code.

#include "SupervisorGroupPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using namespace ai::supervisor;
namespace gm = ai::group_mgr;
namespace mi = ai::manager_inbox;

Cadence const kCad = FullRate(60000, 10000);
gm::Verb const kChangeVerb = gm::Verb(2);   // the T33 verb, by value

bool IsAck(AckDecision a, std::uint8_t state, std::uint8_t reason)
{
    return a.state == state && a.reason == reason;
}

std::uint8_t const kNone = std::uint8_t(Deny::None);

Event Directive(std::uint32_t seq, gm::Verb verb, std::uint16_t serial, std::uint32_t quest,
    gm::GroupType type = gm::GroupType::Quest, bool gatherFlag = false, std::uint8_t gatherKind = 0)
{
    gm::DirectiveBits d;
    d.verb = verb;
    d.type = type;
    d.serial = serial;
    d.gatherFlag = gatherFlag;
    d.gatherKind = gatherKind;
    if (verb == gm::Verb::Exit)
        d.reason = gm::EndReason::NoProgress;
    return gm::MakeDirective(seq, d, quest, 1, 10.0f, 20.0f, 0);
}

Event Other(EventKind kind)
{
    Event e;
    e.kind = kind;
    return e;
}

// ---- the two bodies of GroupHandlerOn (BAUSPEC 0.3), as model functions -------------------------------------------------------
std::uint32_t g_handlerKey = 0;   // model of AiPlayerbot.Supervisor.GroupHandler (Q4 (b)/(c) only)

bool HandlerOnA(std::uint32_t guidLow)   // Q4 (a), built now
{
    (void)guidLow;
    return true;
}

bool HandlerOnB(std::uint32_t guidLow)   // Q4 (b)/(c), only on the owner's word
{
    return g_handlerKey && ai::cohort::Applies(g_handlerKey, guidLow);
}

// ---- the caller model ---------------------------------------------------------------------------------------------------------
struct Bot
{
    // switch and identity
    std::uint32_t guid = 11;
    std::uint32_t arbiter = 2;
    bool (*handlerOn)(std::uint32_t) = HandlerOnA;
    // the supervisor's plain members (Supervisor.h)
    Mode mode = Mode::Quest;
    bool hasPendingDir = false;
    Event pendingDir;
    bool groupHeld = false;
    std::uint16_t groupSerial = 0;
    std::uint32_t groupQuest = 0;
    std::uint32_t leaderLow = 0;
    std::uint8_t gatherKind = 0;
    bool gatherFlag = false;
    bool rescueByGroup = false;
    bool adoptDue = false;
    bool roleByHandler = false;
    std::uint8_t followPhase = 0;
    bool leaderFar = false;
    bool everNear = false;
    bool selfCounted = false;
    std::uint32_t orphanFalseSinceMs = 0;
    std::uint32_t orphanNextAskMs = 0;
    std::uint32_t nextSecondMs = 0;
    std::uint16_t selfExitSerial = 0;
    std::uint32_t selfExitSinceMs = 0;
    std::uint16_t leftRetrySerial = 0;
    std::uint8_t leftRetryReason = 0;
    std::uint32_t closeAtMs[kCloseRingSlots] = {};
    std::uint8_t closeHead = 0;
    std::uint8_t closeUsed = 0;
    bool closeBurst = false;
    bool closeWarn = false;
    ArbiterGrpMinute cnt;
    // the world as the bot thread sees it
    bool alive = true;
    bool managed = true;
    bool inLog = true;
    bool complete = false;
    bool turnedIn = false;
    std::uint32_t coreLeader = 0;
    bool leaderNear = true;
    bool intact = false;
    std::uint8_t role = 0;
    LiveClass live = LiveClass::None;
    bool groupOwns = false;
    bool postOk = true;
    bool writeOk = true;
    std::uint32_t focus = 0;
    bool startZoneHold = false;
    // what the model did
    std::vector<AckDecision> acks;
    std::vector<std::uint8_t> lefts;        // left posts that returned true, by reason
    std::vector<Mode> modeAtLeft;           // the mode when the left was posted
    int undo = 0;
    int makeFollower = 0;
    int makeTurnIn = 0;
    int asks = 0;
    int modeLines = 0;
    int followChecks = 0;

    std::uint32_t N(GrpField f) const
    {
        return cnt.v[std::size_t(f)];
    }

    void Count(GrpField f)
    {
        ++cnt.v[std::size_t(f)];
    }

    std::uint32_t GroupGoalQuest() const
    {
        return mode == Mode::Group ? groupQuest : 0;
    }

    bool Switch(Mode to, Requester who)
    {
        TransitionResult const r = Decide(mode, to, who, Phase::Plan, 3600000, kCad);
        if (r.allowed)
            mode = r.to;
        return r.allowed;
    }

    void TakeMailModel(Event const* mail, std::size_t n)
    {
        std::size_t superseded = 0;
        std::size_t const newest = NewestDirective(mail, n, superseded);
        for (std::size_t i = 0; i < n; ++i)
            if (mail[i].kind == EventKind::GroupDirective)
                Count(DirField(ClassifyVerb(gm::UnpackDirective(mail[i].b).verb)));
        for (std::size_t i = 0; i < superseded; ++i)
            Count(GrpField::Superseded);
        if (newest < n)
        {
            pendingDir = mail[newest];
            hasPendingDir = true;
        }
    }

    void Mail(Event const& e)
    {
        TakeMailModel(&e, 1);
    }

    void Unfollow()
    {
        role = 0;
        roleByHandler = false;
        followPhase = std::uint8_t(FollowPhase::None);
        leaderFar = false;
        intact = false;
        ++undo;
    }

    void FollowCheck()
    {
        ++followChecks;
        bool const near = leaderLow != 0 && leaderNear;
        FollowPhase const phase = FollowPhase(followPhase);
        if (FollowSelfEdge(phase, near, everNear, selfCounted))
        {
            Count(GrpField::FollowSelf);
            selfCounted = true;
        }
        if (near)
            everNear = true;
        FollowDecision const d = FollowStep(phase, near, intact, complete, leaderFar);
        if (d.makeTurnIn)
        {
            ++makeTurnIn;
            Count(GrpField::FollowTurnin);
        }
        if (d.makeFollower)
        {
            ++makeFollower;
            intact = true;
            Count(GrpField::FollowMake);
        }
        // As SupervisorGroup.cpp FollowCheck: leader_far only after the leader was near once (else follow_self, exclusive).
        if (d.countLeaderFar && everNear)
            Count(GrpField::LeaderFar);
        leaderFar = d.leaderFar;
        followPhase = std::uint8_t(d.newPhase);
    }

    void StartFollow()
    {
        role = 1;
        roleByHandler = true;
        followPhase = std::uint8_t(FollowPhase::Follow);
        FollowCheck();
    }

    void Close(RcClose what, std::uint32_t now)
    {
        if (what == RcClose::None)
            return;
        Count(what == RcClose::Expire ? GrpField::GoalClose : GrpField::PrepCancel);
        live = LiveClass::None;
        CloseEdges const e = ClosePush(closeAtMs, closeHead, closeUsed, closeBurst, closeWarn, now);
        if (e.burstEdge)
            Count(GrpField::CloseBurst);
        if (e.warnEdge)
            Count(GrpField::CloseWarn);
    }

    // LeaveGroupMode: rescueBranch = the RESCUE branch (no group target close, no Decide).
    void Leave(bool rescueBranch)
    {
        if (roleByHandler)
            Unfollow();
        if (!rescueBranch && groupOwns)
        {
            groupOwns = false;
            live = LiveClass::None;
        }
        groupHeld = false;
        groupSerial = 0;
        groupQuest = 0;
        leaderLow = 0;
        followPhase = std::uint8_t(FollowPhase::None);
        OrphanReset(orphanFalseSinceMs, orphanNextAskMs);
        adoptDue = false;
        everNear = false;
        selfCounted = false;
        leaderFar = false;
        ++modeLines;
    }

    void Release()   // GroupRelease
    {
        if (groupHeld)
            Leave(mode == Mode::Rescue);
    }

    void Entry(gm::DirectiveBits const& d, Event const& e, std::uint32_t now, bool move)
    {
        std::uint32_t const newLeader = coreLeader;
        if (move && MoveUndoFollowFirst(roleByHandler, newLeader == guid, newLeader == leaderLow,
                FollowPhase(followPhase) == FollowPhase::Follow))
            Unfollow();
        groupHeld = true;
        groupSerial = d.serial;
        groupQuest = e.c;
        gatherKind = d.gatherKind;
        gatherFlag = d.gatherFlag;
        leaderLow = newLeader;
        OrphanReset(orphanFalseSinceMs, orphanNextAskMs);
        adoptDue = true;
        everNear = false;
        selfCounted = false;
        leaderFar = false;
        Close(RcCheck(live, true), now);
        if (ChooseMove(gatherFlag, gatherKind, leaderLow == guid) == MoveKind::Follow)
        {
            // A move to another leader undid the follow above (Unfollow: intact = false); the same leader keeps its follow set.
            if (roleByHandler)
                FollowCheck();
            else
                StartFollow();
        }
    }

    void SendLeft(std::uint16_t serial, std::uint8_t reason)
    {
        modeAtLeft.push_back(mode);
        if (postOk)
        {
            Count(GrpField::Left);
            lefts.push_back(reason);
        }
        else
            LeftArm(leftRetrySerial, leftRetryReason, serial, reason);
    }

    void Apply(Event const& e, std::uint32_t now)
    {
        gm::DirectiveBits const d = gm::UnpackDirective(e.b);
        VerbClass const vc = ClassifyVerb(d.verb);
        bool const arbiter2 = arbiter == 2;
        bool const on = handlerOn(guid);
        bool const same = groupHeld && d.serial == groupSerial;
        AckDecision ack = BadVerbAck();
        ExitClass exitKind = ExitClass::ExitStale;
        switch (vc)
        {
            case VerbClass::Enter:
            {
                EnterFacts f;
                f.arbiter2 = arbiter2;
                f.handlerOn = on;
                f.type = std::uint8_t(d.type);
                f.inGroup = mode == Mode::Group;
                f.sameSerial = same;
                f.inRescue = mode == Mode::Rescue;
                f.roleNotByHandler = role != 0 && !roleByHandler;
                f.startZoneHold = startZoneHold;
                f.goalInLog = inLog;
                f.goalDone = complete;
                f.goalTurnedIn = turnedIn;
                f.focusQuest = focus;
                f.c = e.c;
                ack = EnterCheck(f);
                if (EnterIdempotent(f))
                    break;
                if (EnterIsMove(f))
                {
                    if (AckApplied(ack))
                        Entry(d, e, now, true);
                    else
                    {
                        Leave(false);
                        Switch(Mode::Quest, Requester::Self);
                    }
                    break;
                }
                if (!AckApplied(ack))
                    break;
                ack = AckFromDecide(Decide(mode, Mode::Group, Requester::GroupMgr, Phase::Plan, 3600000, kCad));
                if (!AckApplied(ack))
                    break;
                if (!writeOk)   // write first, then commit: a failed own write rolls back
                {
                    ack = AckNo(gm::kAckReasonLaneBusy);
                    break;
                }
                mode = Mode::Group;
                Entry(d, e, now, false);
                break;
            }
            case VerbClass::Change:
            {
                ChangeFacts c;
                c.arbiter2 = arbiter2;
                c.handlerOn = on;
                c.inGroup = mode == Mode::Group;
                c.sameSerial = same;
                ack = ChangeCheck(c);
                if (AckApplied(ack))
                {
                    gatherKind = d.gatherKind;
                    gatherFlag = d.gatherFlag;
                }
                break;
            }
            case VerbClass::Exit:
            {
                ExitDecision const x = ExitCheck(mode == Mode::Group && groupHeld, mode == Mode::Rescue && groupHeld, same);
                ack = x.ack;
                exitKind = x.kind;
                if (LeftCancel(leftRetrySerial, leftRetryReason, d.serial))
                    Count(GrpField::LeftCancel);
                if (SelfExitEnds(selfExitSerial, selfExitSinceMs, now, d.serial))
                    selfExitSerial = 0;
                if (x.kind == ExitClass::ExitApplied)
                {
                    Leave(false);
                    Switch(Mode::Quest, Requester::GroupMgr);
                }
                else if (x.kind == ExitClass::ExitInRescue)
                    Leave(true);
                break;
            }
            case VerbClass::Bad:
                break;
        }
        // SendAck: exactly one ack; the decision counts also when the post fails.
        acks.push_back(ack);
        Count(AckOutcomeField(vc, ack, exitKind));
        GrpField rf = GrpField::Count;
        if (!AckApplied(ack) && ReasonField(ack.reason, rf))
            Count(rf);
        if (!postOk)
            Count(AckDropField(vc));
    }

    void Tick(std::uint32_t now)
    {
        // step 0
        if (mode != Mode::Rescue)
            rescueByGroup = false;
        bool const arbiter2 = arbiter == 2;
        // step 8 (right after the owner sample of step 2, which this model leaves out)
        if (mode != Mode::Group && mode != Mode::Rescue && !hasPendingDir && leftRetrySerial == 0 && selfExitSerial == 0)
            return;
        // step 3
        HandlerLeave const hl = HandlerLeaveCause(arbiter2, handlerOn(guid), groupHeld);
        if (hl != HandlerLeave::None)
        {
            if (hl == HandlerLeave::HandlerOff)
                Count(GrpField::HandlerOff);
            if (mode == Mode::Group)
            {
                Leave(false);
                Switch(Mode::Quest, Requester::Self);
            }
            else
                Leave(true);
        }
        if (SelfExitEnds(selfExitSerial, selfExitSinceMs, now, 0))
            selfExitSerial = 0;
        // step 4
        if (hasPendingDir)
        {
            hasPendingDir = false;
            Apply(pendingDir, now);
        }
        // step 5
        if (leftRetrySerial != 0)
        {
            std::uint8_t const reason = leftRetryReason;
            LeftStep const s = LeftRetryDone(leftRetrySerial, leftRetryReason, postOk);
            if (s == LeftStep::Sent)
            {
                Count(GrpField::Left);
                lefts.push_back(reason);
            }
            else if (s == LeftStep::Dropped)
                Count(GrpField::LeftDrop);
        }
        // step 6
        if (mode == Mode::Group && groupHeld)
        {
            if (PeriodDue(nextSecondMs, now, kGroupCheckMs))
            {
                GoalEnd const end = ClassifyGoal(turnedIn, inLog);
                if (end != GoalEnd::None)
                {
                    std::uint16_t const serial = groupSerial;   // the leave forgets the group: keep the serial for the left
                    Leave(false);
                    Switch(Mode::Quest, Requester::Self);
                    Count(end == GoalEnd::Done ? GrpField::GoalDone : GrpField::GoalLost);
                    SendLeft(serial, GoalEndReason(end));
                    selfExitSerial = serial;
                    selfExitSinceMs = now;
                    return;
                }
                Close(RcCheck(live, false), now);
                if (roleByHandler && FollowPhase(followPhase) == FollowPhase::Follow)
                    FollowCheck();
                AdoptKind const a = AdoptCheck(adoptDue, live, groupOwns);
                if (a != AdoptKind::None)
                {
                    Count(a == AdoptKind::Enter ? GrpField::GoalAdoptEnter : GrpField::CrossKeep);
                    groupOwns = true;
                }
            }
            if (OrphanAskTake(orphanNextAskMs, now))
            {
                ++asks;
                if (OrphanStep(orphanFalseSinceMs, now, managed))
                {
                    Count(GrpField::Orphan);
                    Leave(false);
                    Switch(Mode::Quest, Requester::Self);
                }
            }
        }
        else if (mode == Mode::Rescue && groupHeld && !alive && OrphanAskTake(orphanNextAskMs, now))
        {
            ++asks;
            if (OrphanStep(orphanFalseSinceMs, now, managed))
            {
                Count(GrpField::Orphan);
                Leave(true);
            }
        }
        // step 7
        if (mode == Mode::Group && !alive)
        {
            Switch(Mode::Rescue, Requester::Self);
            OrphanReset(orphanFalseSinceMs, orphanNextAskMs);
            rescueByGroup = true;
            Count(GrpField::RescueIn);
        }
        else if (mode == Mode::Rescue)
        {
            bool managedNow = false;
            if (rescueByGroup && alive && groupHeld)
            {
                ++asks;   // the fresh first question after the revival, outside the 10 s gate
                managedNow = managed;
            }
            switch (RescueReturn(rescueByGroup, alive, groupHeld, managedNow))
            {
                case RescueEnd::ToGroup:
                    Switch(Mode::Group, Requester::Self);
                    OrphanResetAsked(orphanFalseSinceMs, orphanNextAskMs, now);   // the fresh question is this window's ask
                    Close(RcCheck(live, true), now);
                    if (roleByHandler && FollowPhase(followPhase) == FollowPhase::Follow)
                        FollowCheck();
                    adoptDue = true;
                    Count(GrpField::RescueGroup);
                    break;
                case RescueEnd::ToQuestLeave:
                    Leave(true);
                    Switch(Mode::Quest, Requester::Self);
                    rescueByGroup = false;
                    Count(GrpField::RescueBack);
                    break;
                case RescueEnd::ToQuestNoGroup:
                    Switch(Mode::Quest, Requester::Self);
                    rescueByGroup = false;
                    ++modeLines;   // ev=mode cause=rescue_end
                    Count(GrpField::RescueBack);
                    break;
                case RescueEnd::Stay:
                    break;
            }
        }
    }
};

// A follower bot (guid 11) whose core leader is guid 13, entered into QUEST group serial 7 on quest 500 at t = 1000.
Bot Entered(std::uint32_t& t)
{
    Bot b;
    b.coreLeader = 13;
    t = 1000;
    b.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    b.Tick(t);
    Require(b.acks.size() == 1 && IsAck(b.acks[0], mi::kAckApplied, kNone), "Enter: 2/0");
    Require(b.mode == Mode::Group && b.groupHeld && b.groupSerial == 7 && b.GroupGoalQuest() == 500, "Enter: GROUP with goal c");
    return b;
}

// ---- pure functions -------------------------------------------------------------------------------------------------------------
void TestClassifyVerb()
{
    for (std::uint32_t v = 0; v < 16; ++v)
    {
        VerbClass const c = ClassifyVerb(gm::Verb(v));
        VerbClass const want = v == 1 ? VerbClass::Enter : v == 2 ? VerbClass::Change : v == 3 ? VerbClass::Exit : VerbClass::Bad;
        Require(c == want, "ClassifyVerb for 0-15: 1 Enter, 2 Change, 3 Exit, else Bad");
        // the verb survives the bits: the class of the unpacked directive is the class of the raw verb
        gm::DirectiveBits d;
        d.verb = gm::Verb(v);
        d.serial = 3;
        Require(ClassifyVerb(gm::UnpackDirective(gm::PackDirective(d)).verb) == want, "the class survives Pack/Unpack");
    }
    Require(DirField(VerbClass::Enter) == GrpField::DirEnter && DirField(VerbClass::Change) == GrpField::DirUpdate &&
        DirField(VerbClass::Exit) == GrpField::DirExit && DirField(VerbClass::Bad) == GrpField::DirBad, "the dir_* field per class");
}

void TestNewestDirective()
{
    std::size_t sup = 99;
    Require(NewestDirective(nullptr, 0, sup) == 0 && sup == 0, "an empty drain: none");
    Event mail[6];
    mail[0] = Other(EventKind::QuestAccepted);
    mail[1] = Directive(1, gm::Verb::Enter, 7, 500);
    mail[2] = Other(EventKind::CombatStart);
    mail[3] = Directive(2, gm::Verb::Exit, 7, 500);
    mail[4] = Directive(3, gm::Verb::Enter, 8, 600);
    mail[5] = Other(EventKind::TravelStatus);
    Require(NewestDirective(mail, 1, sup) == 1 && sup == 0, "no directive: index n");
    Require(NewestDirective(mail, 2, sup) == 1 && sup == 0, "one directive: itself, none superseded");
    Require(NewestDirective(mail, 4, sup) == 3 && sup == 1, "two directives: the newest, one superseded");
    Require(NewestDirective(mail, 6, sup) == 4 && sup == 2, "three directives: the newest, two superseded");
    Event four[4] = {Directive(1, gm::Verb::Enter, 7, 1), Directive(2, gm::Verb::Exit, 7, 1), Directive(3, gm::Verb::Enter, 9, 2),
        Directive(4, gm::Verb::Exit, 9, 2)};
    Require(NewestDirective(four, 4, sup) == 3 && sup == 3, "four directives: the last, three superseded");

    Bot b;
    b.TakeMailModel(four, 4);
    Require(b.N(GrpField::DirEnter) == 2 && b.N(GrpField::DirExit) == 2 && b.N(GrpField::Superseded) == 3, "HandleMail counts");
    Require(b.hasPendingDir && b.pendingDir.a == 4 && b.acks.empty() && b.mode == Mode::Quest, "HandleMail keeps the newest, writes nothing");
}

void TestEnterOrder()
{
    EnterFacts f;
    f.arbiter2 = true;
    f.handlerOn = true;
    f.type = std::uint8_t(gm::GroupType::Quest);
    f.goalInLog = true;
    f.c = 500;
    Require(IsAck(EnterCheck(f), mi::kAckApplied, kNone), "all checks pass: 2/0 (Decide next)");
    // every check failing at once: the first in the order wins, then peel one at a time
    EnterFacts g = f;
    g.arbiter2 = false;
    g.type = std::uint8_t(gm::GroupType::Dungeon);
    g.inRescue = true;
    g.roleNotByHandler = true;
    g.startZoneHold = true;
    g.goalInLog = false;
    g.focusQuest = 9;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonArbiterOff), "3/22 first");
    g.arbiter2 = true;
    g.handlerOn = false;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonArbiterOff), "handler off: 3/22");
    g.handlerOn = true;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, std::uint8_t(Deny::NotAMode)), "then 3/2 (type)");
    g.type = std::uint8_t(gm::GroupType::Adhoc);
    Require(IsAck(EnterCheck(g), mi::kAckRefused, std::uint8_t(Deny::RescueFirst)), "then 3/7 (RESCUE)");
    g.inRescue = false;
    g.inGroup = true;
    g.sameSerial = true;
    Require(IsAck(EnterCheck(g), mi::kAckApplied, kNone) && EnterIdempotent(g), "then the idempotent 2/0");
    g.sameSerial = false;
    Require(EnterIsMove(g) && !EnterIdempotent(g), "another serial in GROUP is a move");
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonLegacyGroupRole), "(1) 3/21");
    g.roleNotByHandler = false;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonStartZoneHold), "(2) 3/16");
    g.startZoneHold = false;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonGoalNotInLog), "(3) 3/24 not in the log");
    g.goalInLog = true;
    g.goalDone = true;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonGoalNotInLog), "(3) 3/24 complete");
    g.goalDone = false;
    g.goalTurnedIn = true;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonGoalNotInLog), "(3) 3/24 turned in");
    g.goalTurnedIn = false;
    g.c = 0;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonGoalNotInLog), "(3) 3/24 without c (fail closed)");
    g.c = 500;
    Require(IsAck(EnterCheck(g), mi::kAckRefused, gm::kAckReasonFocusConflict), "(4) 3/25");
    g.focusQuest = 500;
    Require(IsAck(EnterCheck(g), mi::kAckApplied, kNone), "focus on c: 2/0");
    g.type = std::uint8_t(gm::GroupType::Dungeon);
    g.sameSerial = true;
    Require(!EnterIdempotent(g), "a bad type is never idempotent");

    // Decide mapped to the ack
    Require(IsAck(AckFromDecide(Decide(Mode::Quest, Mode::Group, Requester::GroupMgr, Phase::Plan, 0, kCad)), mi::kAckApplied, kNone),
        "QUEST -> GROUP: 2/0");
    Require(IsAck(AckFromDecide(Decide(Mode::Passive, Mode::Group, Requester::GroupMgr, Phase::Plan, 0, kCad)), mi::kAckRefused,
        std::uint8_t(Deny::PassiveHold)), "PASSIVE: 3/4");
    Require(IsAck(AckFromDecide(Decide(Mode::Rescue, Mode::Group, Requester::GroupMgr, Phase::Plan, 0, kCad)), mi::kAckRefused,
        std::uint8_t(Deny::RescueFirst)), "RESCUE: 3/7");
    Require(IsAck(AckFromDecide(Decide(Mode::Quest, Mode::Group, Requester::GroupMgr, Phase::RegroupRest, 0, kCad)), mi::kAckRefused,
        std::uint8_t(Deny::Phase)), "RegroupRest outside GROUP: 3/6");
    Require(IsAck(BadVerbAck(), mi::kAckRefused, std::uint8_t(Deny::NotAMode)), "a bad verb: 3/2");
}

void TestChangeExit()
{
    ChangeFacts c;
    Require(IsAck(ChangeCheck(c), mi::kAckRefused, gm::kAckReasonArbiterOff), "Change at Arbiter != 2: 3/22");
    c.arbiter2 = true;
    c.handlerOn = true;
    Require(IsAck(ChangeCheck(c), mi::kAckRefused, gm::kAckReasonNoOpenGroup), "Change outside GROUP (also RESCUE): 3/20");
    c.inGroup = true;
    Require(IsAck(ChangeCheck(c), mi::kAckRefused, gm::kAckReasonNoOpenGroup), "Change of another serial: 3/20");
    c.sameSerial = true;
    c.pointOtherMap = true;
    c.pointStartZoneHold = true;
    Require(IsAck(ChangeCheck(c), mi::kAckRefused, gm::kAckReasonGatherOtherMap), "point on another map: 3/19");
    c.pointOtherMap = false;
    Require(IsAck(ChangeCheck(c), mi::kAckRefused, gm::kAckReasonStartZoneHold), "point outside the start zone: 3/16");
    c.pointStartZoneHold = false;
    Require(IsAck(ChangeCheck(c), mi::kAckApplied, kNone), "Change in GROUP, same serial: 2/0");

    for (int m = 0; m < 8; ++m)
    {
        ExitDecision const x = ExitCheck((m & 1) != 0, (m & 2) != 0, (m & 4) != 0);
        Require(IsAck(x.ack, mi::kAckApplied, kNone), "an Exit is always 2/0");
    }
    Require(ExitCheck(true, false, true).kind == ExitClass::ExitApplied, "GROUP, same serial: applied");
    Require(ExitCheck(false, true, true).kind == ExitClass::ExitInRescue, "RESCUE with the group: in rescue");
    Require(ExitCheck(true, false, false).kind == ExitClass::ExitStale && ExitCheck(false, false, false).kind == ExitClass::ExitStale &&
        ExitCheck(false, true, false).kind == ExitClass::ExitStale, "anything else: stale");
}

void TestAckFields()
{
    AckDecision const ok = AckOk();
    AckDecision const no = AckNo(gm::kAckReasonGoalNotInLog);
    Require(AckOutcomeField(VerbClass::Enter, ok, ExitClass::ExitStale) == GrpField::EnterOk &&
        AckOutcomeField(VerbClass::Enter, no, ExitClass::ExitStale) == GrpField::EnterRef, "enter_ok / enter_ref");
    Require(AckOutcomeField(VerbClass::Change, ok, ExitClass::ExitStale) == GrpField::UpdateOk &&
        AckOutcomeField(VerbClass::Change, no, ExitClass::ExitStale) == GrpField::UpdateRef, "update_ok / update_ref");
    Require(AckOutcomeField(VerbClass::Exit, ok, ExitClass::ExitApplied) == GrpField::ExitOk &&
        AckOutcomeField(VerbClass::Exit, ok, ExitClass::ExitInRescue) == GrpField::ExitOk &&
        AckOutcomeField(VerbClass::Exit, ok, ExitClass::ExitStale) == GrpField::ExitStale, "exit_ok / exit_stale");
    Require(AckOutcomeField(VerbClass::Bad, BadVerbAck(), ExitClass::ExitStale) == GrpField::BadRef, "bad_ref");
    Require(AckDropField(VerbClass::Exit) == GrpField::AckDropExit && AckDropField(VerbClass::Enter) == GrpField::AckDropEnter &&
        AckDropField(VerbClass::Change) == GrpField::AckDropEnter && AckDropField(VerbClass::Bad) == GrpField::AckDropEnter,
        "ack_drop_exit for Exit only, ack_drop_enter for the rest");
    for (std::uint32_t r = 0; r < 256; ++r)
    {
        GrpField f = GrpField::Count;
        bool const has = ReasonField(std::uint8_t(r), f);
        bool const want = (r >= 1 && r <= 7) || r == 16 || (r >= 19 && r <= 25);
        Require(has == want, "a reason field for 1-7, 16, 19-25 only");
        if (r >= 1 && r <= 7)
            Require(f == GrpField::RDeny, "1-7: r_deny");
    }
    GrpField f = GrpField::Count;
    Require(ReasonField(gm::kAckReasonStartZoneHold, f) && f == GrpField::R16, "r16");
    Require(ReasonField(gm::kAckReasonGatherOtherMap, f) && f == GrpField::R19, "r19");
    Require(ReasonField(gm::kAckReasonNoOpenGroup, f) && f == GrpField::R20, "r20");
    Require(ReasonField(gm::kAckReasonLegacyGroupRole, f) && f == GrpField::R21, "r21");
    Require(ReasonField(gm::kAckReasonArbiterOff, f) && f == GrpField::R22, "r22");
    Require(ReasonField(gm::kAckReasonLaneBusy, f) && f == GrpField::R23, "r23");
    Require(ReasonField(gm::kAckReasonGoalNotInLog, f) && f == GrpField::R24, "r24");
    Require(ReasonField(gm::kAckReasonFocusConflict, f) && f == GrpField::R25, "r25");
    Require(!ReasonField(gm::kAckReasonGoalDone, f) && !ReasonField(gm::kAckReasonGoalLost, f), "26/27 are left reasons only");
}

void TestOrphanClock()
{
    OrphanClock c;
    Require(OrphanAskTake(c, 5000), "the first question is due at once");
    Require(!OrphanAskTake(c, 5000 + kOrphanAskMs - 1) && OrphanAskTake(c, 5000 + kOrphanAskMs), "then every 10 s");
    Require(!OrphanStep(c, 10000, false), "the first false starts the clock");
    Require(!OrphanStep(c, 10000 + gm::kOrphanGraceMs - 1, false), "not before the grace");
    Require(OrphanStep(c, 10000 + gm::kOrphanGraceMs, false), "orphan after kOrphanGraceMs false in a row");
    OrphanClock d;
    Require(!OrphanStep(d, 0, false) && d.falseSinceMs != 0, "a false at clock 0 still starts the clock");
    Require(!OrphanStep(d, 30000, true) && d.falseSinceMs == 0, "a true resets the run");
    Require(!OrphanStep(d, 40000, false) && !OrphanStep(d, 40000 + gm::kOrphanGraceMs - 1, false), "a new run starts over");
    // across the clock wrap
    OrphanClock w;
    Require(!OrphanStep(w, 0xFFFFF000u, false) && OrphanStep(w, 0xFFFFF000u + gm::kOrphanGraceMs, false), "the grace across the wrap");
    // RESCUE entry or exit resets both: the first question after the revival is fresh
    OrphanClock r;
    OrphanAskTake(r, 1000);
    OrphanStep(r, 1000, false);
    OrphanReset(r);
    Require(r.falseSinceMs == 0 && r.nextAskMs == 0 && OrphanAskTake(r, 1001), "reset: fresh question, no running clock");
    std::uint32_t since = 5;
    std::uint32_t next = 6;
    OrphanReset(since, next);
    Require(since == 0 && next == 0, "the plain-field reset");
    since = 5;
    next = 0;
    OrphanResetAsked(since, next, 20000);
    Require(since == 0 && next == 20000 + kOrphanAskMs && !OrphanAskTake(next, 20000 + kOrphanAskMs - 1) &&
        OrphanAskTake(next, 20000 + kOrphanAskMs), "RESCUE -> GROUP: the fresh question counts, the next one 10 s later");
    std::uint32_t gate = 0;
    Require(PeriodDue(gate, 0xFFFFFC18u, kGroupCheckMs) && gate == 0x00000000u + 1u, "a period gate never stores 0");
}

void TestGoalEnd()
{
    Require(ClassifyGoal(true, false) == GoalEnd::Done && ClassifyGoal(true, true) == GoalEnd::Done, "turned in: done");
    Require(ClassifyGoal(false, false) == GoalEnd::Lost, "neither in the log nor turned in: lost");
    Require(ClassifyGoal(false, true) == GoalEnd::None, "in the log: none");
    Require(GoalEndReason(GoalEnd::Done) == gm::kAckReasonGoalDone && GoalEndReason(GoalEnd::Lost) == gm::kAckReasonGoalLost,
        "26 goal_done, 27 goal_lost");
}

void TestLiveClass()
{
    Require(ClassifyLive(false, false, false, false, 0, 500) == LiveClass::None, "no target");
    Require(ClassifyLive(true, true, false, true, 77, 500) == LiveClass::Prepare, "PREPARE");
    Require(ClassifyLive(true, false, false, true, 77, 500) == LiveClass::None, "a closed target");
    Require(ClassifyLive(true, false, true, true, 77, 500) == LiveClass::QuestOther, "quest != c");
    Require(ClassifyLive(true, false, true, true, 500, 500) == LiveClass::QuestGoal, "quest = c");
    Require(ClassifyLive(true, false, true, false, 0, 500) == LiveClass::NonQuest, "non-quest");
    Require(RcCheck(LiveClass::QuestOther, true) == RcClose::Expire && RcCheck(LiveClass::QuestOther, false) == RcClose::Expire,
        "quest != c: expired at entry and in the 1/s check");
    Require(RcCheck(LiveClass::Prepare, true) == RcClose::Cancel && RcCheck(LiveClass::Prepare, false) == RcClose::None,
        "PREPARE: cancelled at entry only");
    Require(RcCheck(LiveClass::QuestGoal, true) == RcClose::None && RcCheck(LiveClass::NonQuest, true) == RcClose::None &&
        RcCheck(LiveClass::None, true) == RcClose::None, "c, non-quest and nothing stay");

    bool due = true;
    Require(AdoptCheck(due, LiveClass::QuestGoal, false) == AdoptKind::Enter && !due, "the first check after an entry: goal_adopt_enter");
    Require(AdoptCheck(due, LiveClass::QuestGoal, false) == AdoptKind::Cross, "later: cross_keep");
    due = true;
    Require(AdoptCheck(due, LiveClass::NonQuest, false) == AdoptKind::None && !due, "adoptDue is cleared in any case");
    due = true;
    Require(AdoptCheck(due, LiveClass::QuestGoal, true) == AdoptKind::None && !due, "Group already holds it: nothing");
}

void TestChooseMove()
{
    for (std::uint8_t kind = 0; kind < 4; ++kind)
    {
        Require(ChooseMove(false, kind, true) == MoveKind::LeaderTravel && ChooseMove(false, kind, false) == MoveKind::Follow,
            "flag 0: the leader travels, the others follow (any kind)");
    }
    Require(ChooseMove(true, kGatherFollow, true) == MoveKind::LeaderTravel && ChooseMove(true, kGatherFollow, false) == MoveKind::Follow,
        "kind a: as flag 0");
    Require(ChooseMove(true, kGatherAtLeader, true) == MoveKind::LeaderTravel && ChooseMove(true, kGatherAtLeader, false) ==
        MoveKind::PointGo, "kind b: leader travels, the others go to the point");
    Require(ChooseMove(true, kGatherMeet, true) == MoveKind::PointGo && ChooseMove(true, kGatherMeet, false) == MoveKind::PointGo &&
        ChooseMove(true, kGatherMeet, true, true) == MoveKind::PointWait && ChooseMove(true, kGatherMeet, false, true) == MoveKind::PointGo,
        "kind c: all go to the point, the leader waits there");
    Require(ChooseMove(true, 3, false) == MoveKind::Follow, "kind 3 (undefined): as kind a");
}

void TestFollowStep()
{
    FollowDecision d = FollowStep(FollowPhase::None, true, false, false, false);
    Require(!d.makeFollower && !d.makeTurnIn && !d.countLeaderFar && d.newPhase == FollowPhase::None, "not following: nothing");
    d = FollowStep(FollowPhase::Follow, true, false, false, false);
    Require(d.makeFollower && !d.countLeaderFar && d.newPhase == FollowPhase::Follow && !d.leaderFar, "near, broken: MakeFollower");
    d = FollowStep(FollowPhase::Follow, true, true, false, false);
    Require(!d.makeFollower, "near, intact: nothing");
    d = FollowStep(FollowPhase::Follow, false, false, false, false);
    Require(!d.makeFollower && d.countLeaderFar && d.leaderFar, "near -> far: leader_far once, no MakeFollower");
    d = FollowStep(FollowPhase::Follow, false, false, false, true);
    Require(!d.makeFollower && !d.countLeaderFar && d.leaderFar, "still far: not counted again");
    d = FollowStep(FollowPhase::Follow, true, false, false, true);
    Require(d.makeFollower && !d.leaderFar, "far -> near: follow again");
    d = FollowStep(FollowPhase::Follow, false, false, true, false);
    Require(d.makeTurnIn && d.newPhase == FollowPhase::TurnIn && !d.makeFollower && !d.countLeaderFar, "complete: TurnIn");
    d = FollowStep(FollowPhase::TurnIn, true, false, true, false);
    Require(!d.makeTurnIn && !d.makeFollower && d.newPhase == FollowPhase::TurnIn, "TurnIn once, no follower check afterwards");
    Require(FollowSelfEdge(FollowPhase::Follow, false, false, false), "a follower without a near leader so far: follow_self");
    Require(!FollowSelfEdge(FollowPhase::Follow, false, false, true) && !FollowSelfEdge(FollowPhase::Follow, false, true, false) &&
        !FollowSelfEdge(FollowPhase::Follow, true, false, false) && !FollowSelfEdge(FollowPhase::None, false, false, false),
        "follow_self once, never after the leader was near, never for a non-follower");
    Require(MoveUndoFollowFirst(true, true, false, true) && MoveUndoFollowFirst(true, true, true, true) &&
        !MoveUndoFollowFirst(false, true, false, true), "a follower that becomes the leader undoes the follow first");
    Require(MoveUndoFollowFirst(true, false, false, true), "a move to another leader undoes the old group's follow first");
    Require(MoveUndoFollowFirst(true, false, true, false), "a move out of TurnIn undoes the follow first (the phase starts over)");
    Require(!MoveUndoFollowFirst(true, false, true, true) && !MoveUndoFollowFirst(false, false, false, false),
        "the same leader, still Follow: the follow set stands; no follow by the handler: nothing to undo");
}

void TestCloseRing()
{
    CloseRing r;
    std::uint32_t burstEdges = 0;
    std::uint32_t warnEdges = 0;
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        CloseEdges const e = ClosePush(r, 1000 + i * 1000);
        Require(!e.burstEdge && !e.warnEdge, "fewer than 5 closes: no edge");
    }
    CloseEdges e = ClosePush(r, 5000);
    Require(e.warnEdge && !e.burstEdge && r.warn, "the 5th close in 600 s: warn edge");
    e = ClosePush(r, 6000);
    Require(!e.warnEdge, "warn once per crossing");
    // a loop: one close per second
    for (std::uint32_t i = 0; i < 40; ++i)
    {
        e = ClosePush(r, 7000 + i * 1000);
        burstEdges += e.burstEdge ? 1 : 0;
        warnEdges += e.warnEdge ? 1 : 0;
    }
    Require(burstEdges == 1 && warnEdges == 0 && r.burst, "30 closes in 60 s: one burst edge");
    // quiet for 11 minutes, then one close: both flags fall back; a new crossing counts again
    e = ClosePush(r, 7000 + 40 * 1000 + 660000);
    Require(!e.burstEdge && !e.warnEdge && !r.burst && !r.warn, "the flags fall back below the thresholds");
    std::uint32_t const base = 7000 + 40 * 1000 + 700000;
    std::uint32_t again = 0;
    for (std::uint32_t i = 0; i < 30; ++i)
        again += ClosePush(r, base + i * 100).burstEdge ? 1 : 0;
    Require(again == 1, "a second crossing counts again");
    // the 60 s window: 29 closes 2 s apart, then the 30th 58 s or 60 s after the first
    CloseRing s;
    bool any = false;
    for (std::uint32_t i = 0; i < 29; ++i)
        any = ClosePush(s, 100000 + i * 2000).burstEdge || any;
    Require(!any && !s.burst, "29 closes in 60 s: no burst");
    CloseRing late = s;
    Require(ClosePush(s, 158000).burstEdge && s.burst, "the 30th close 58 s after the first: burst");
    Require(!ClosePush(late, 160000).burstEdge && !late.burst, "60 s after the first, the first is out of the window: 29, no burst");
}

void TestSelfExitAndLeft()
{
    Require(!SelfExitEnds(0, 0, 999999, 0), "no lock: nothing ends");
    Require(SelfExitEnds(7, 1000, 1001, 7), "an Exit with the same serial ends the lock");
    Require(!SelfExitEnds(7, 1000, 1001, 8) && !SelfExitEnds(7, 1000, 1000 + gm::kOrphanGraceMs - 1, 0), "another serial, in time");
    Require(SelfExitEnds(7, 1000, 1000 + gm::kOrphanGraceMs, 0), "the lock ends after kOrphanGraceMs");

    LeftRetry r;
    Require(LeftRetryDone(r, true) == LeftStep::None && !LeftCancel(r, 7), "nothing pending");
    LeftArm(r, 7, gm::kAckReasonGoalDone);
    Require(r.pending && !LeftCancel(r, 8) && r.pending, "an Exit of another serial does not cancel");
    Require(LeftCancel(r, 7) && !r.pending && LeftRetryDone(r, false) == LeftStep::None, "an Exit of the same serial cancels");
    LeftArm(r, 7, gm::kAckReasonGoalLost);
    Require(LeftRetryDone(r, true) == LeftStep::Sent && !r.pending, "the retry posts once: left");
    LeftArm(r, 7, gm::kAckReasonGoalLost);
    Require(LeftRetryDone(r, false) == LeftStep::Dropped && !r.pending, "a second false: left_drop");
    std::uint16_t serial = 0;
    std::uint8_t reason = 0;
    LeftArm(serial, reason, 9, gm::kAckReasonGoalDone);
    Require(serial == 9 && reason == gm::kAckReasonGoalDone && LeftCancel(serial, reason, 9) && serial == 0 && reason == 0,
        "the plain-field left retry");
}

void TestGrpBalanced()
{
    ArbiterGrpMinute sum;
    ArbiterGrpMinute last;
    Require(GrpBalanced(sum, last), "nothing: balanced");
    sum.v[std::size_t(GrpField::DirEnter)] = 5;
    sum.v[std::size_t(GrpField::DirExit)] = 4;
    sum.v[std::size_t(GrpField::DirBad)] = 1;
    sum.v[std::size_t(GrpField::Superseded)] = 1;
    sum.v[std::size_t(GrpField::EnterOk)] = 3;
    sum.v[std::size_t(GrpField::EnterRef)] = 1;
    sum.v[std::size_t(GrpField::ExitOk)] = 2;
    sum.v[std::size_t(GrpField::ExitStale)] = 1;
    sum.v[std::size_t(GrpField::BadRef)] = 1;
    sum.v[std::size_t(GrpField::AckDropEnter)] = 2;   // a subset of the left side, not added
    Require(GrpRight(sum) == 9 && GrpLeft(sum) == 8, "R = 5 + 4 + 1 - 1, L = 3 + 1 + 2 + 1 + 1");
    Require(!GrpBalanced(sum, last), "one open answer with an empty last minute: not balanced");
    last.v[std::size_t(GrpField::DirExit)] = 1;
    Require(GrpBalanced(sum, last), "the open one stands in the last printed minute: balanced");
    sum.v[std::size_t(GrpField::ExitOk)] = 4;
    Require(!GrpBalanced(sum, last), "the left side ahead of the right: never balanced");
}

// ---- the caller model ---------------------------------------------------------------------------------------------------------
void TestEnterFollowerAndLeader()
{
    std::uint32_t t = 0;
    Bot f = Entered(t);
    Require(f.leaderLow == 13 && f.role == 1 && f.roleByHandler && FollowPhase(f.followPhase) == FollowPhase::Follow,
        "a follower: role 1 by the handler, phase Follow");
    Require(f.makeFollower == 1 && f.N(GrpField::FollowMake) == 1 && f.N(GrpField::EnterOk) == 1 && f.N(GrpField::DirEnter) == 1,
        "MakeFollower at Enter (counted follow_make)");
    // at most one follower check per second: 30 ticks of 100 ms
    int const before = f.followChecks;
    for (int i = 1; i <= 30; ++i)
        f.Tick(t + std::uint32_t(i) * 100);
    Require(f.followChecks - before <= 3 && f.followChecks - before >= 2, "the follower check runs at most once per second");
    Require(f.makeFollower == 1, "an intact follow set is not made again");
    f.intact = false;   // ResetStrategies elsewhere lifted the set
    f.Tick(t + 5000);
    Require(f.makeFollower == 2 && f.N(GrpField::FollowMake) == 2, "a broken set is made again at the next check");

    Bot l;
    l.guid = 13;
    l.coreLeader = 13;
    l.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    l.Tick(1000);
    Require(l.mode == Mode::Group && l.role == 0 && !l.roleByHandler && l.makeFollower == 0 && l.followChecks == 0,
        "the leader: no role, no master, no follow set from the handler");
}

void TestFollowerStates()
{
    std::uint32_t t = 0;
    Bot f = Entered(t);
    // leader far: counted once per near -> far edge
    f.leaderNear = false;
    for (int i = 1; i <= 5; ++i)
        f.Tick(t + std::uint32_t(i) * 1000);
    Require(f.N(GrpField::LeaderFar) == 1 && f.makeFollower == 1, "leader far: one leader_far, no MakeFollower");
    f.leaderNear = true;
    f.intact = false;
    f.Tick(t + 6000);
    f.leaderNear = false;
    f.Tick(t + 7000);
    Require(f.N(GrpField::LeaderFar) == 2 && f.makeFollower == 2, "near again, then far again: a second edge");
    Require(f.N(GrpField::FollowSelf) == 0, "the leader was near at Enter: no follow_self");
    // complete: TurnIn once, no follower check afterwards
    f.leaderNear = true;
    f.complete = true;
    f.Tick(t + 8000);
    f.Tick(t + 9000);
    Require(f.makeTurnIn == 1 && f.N(GrpField::FollowTurnin) == 1 && FollowPhase(f.followPhase) == FollowPhase::TurnIn,
        "complete: TurnIn once");
    int const checks = f.followChecks;
    f.Tick(t + 10000);
    Require(f.followChecks == checks, "no follower check after TurnIn");
    // Exit undoes the follow (also after TurnIn)
    f.Mail(Directive(2, gm::Verb::Exit, 7, 500));
    f.Tick(t + 11000);
    Require(f.mode == Mode::Quest && !f.groupHeld && f.undo == 1 && f.role == 0 && FollowPhase(f.followPhase) == FollowPhase::None,
        "Exit: UndoFollow, back to QUEST");
    Require(f.N(GrpField::ExitOk) == 1 && IsAck(f.acks.back(), mi::kAckApplied, kNone), "Exit: 2/0");

    // leader 0 (no core group at Enter): follow_self once, no MakeFollower, never leader_far (that one needs a near leader first)
    Bot s;
    s.coreLeader = 0;
    s.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    s.Tick(1000);
    for (int i = 1; i <= 5; ++i)
        s.Tick(1000 + std::uint32_t(i) * 1000);
    Require(s.mode == Mode::Group && s.role == 1 && s.makeFollower == 0 && s.N(GrpField::FollowSelf) == 1 &&
        s.N(GrpField::LeaderFar) == 0, "leader 0: the bot searches c itself, follow_self once, no leader_far");
    // a leader never near: the same
    Bot n;
    n.coreLeader = 13;
    n.leaderNear = false;
    n.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    n.Tick(1000);
    n.Tick(2000);
    Require(n.N(GrpField::FollowSelf) == 1 && n.makeFollower == 0, "a leader never near: follow_self once");
    n.leaderNear = true;
    n.Tick(3000);
    Require(n.makeFollower == 1 && n.N(GrpField::FollowSelf) == 1, "the leader comes near: the follow set, no second follow_self");
}

void TestUndoPaths()
{
    std::uint32_t t = 0;
    // orphan
    Bot o = Entered(t);
    o.managed = false;
    for (std::uint32_t s = 1; s <= 80; ++s)
        o.Tick(t + s * 1000);
    Require(o.N(GrpField::Orphan) == 1 && o.mode == Mode::Quest && o.undo == 1 && !o.groupHeld, "orphan: leave, undo, QUEST");
    Require(o.asks >= 7 && o.asks <= 9, "the view is asked at most once per 10 s");
    // goal end
    Bot g = Entered(t);
    g.turnedIn = true;
    g.Tick(t + 1000);
    Require(g.undo == 1 && g.mode == Mode::Quest && g.N(GrpField::GoalDone) == 1, "goal end: undo, QUEST");
    // arbiter switch 2 -> 1
    Bot a = Entered(t);
    a.arbiter = 1;
    a.Tick(t + 100);
    Require(a.undo == 1 && a.mode == Mode::Quest && !a.groupHeld && a.N(GrpField::HandlerOff) == 0 && a.lefts.empty(),
        "arbiter 2 -> 1: leave silently, undo, no left, no handler_off");
    // move: follower -> leader
    Bot m = Entered(t);
    m.coreLeader = 11;   // the bot itself leads the new group
    m.Mail(Directive(2, gm::Verb::Enter, 9, 600));
    m.Tick(t + 2000);
    Require(IsAck(m.acks.back(), mi::kAckApplied, kNone) && m.groupSerial == 9 && m.GroupGoalQuest() == 600 && m.leaderLow == 11,
        "move accepted: the new group, the new leader");
    Require(m.undo == 1 && m.role == 0 && !m.roleByHandler, "move follower -> leader: the follow is undone first");
    // move follower -> follower out of TurnIn (same leader): the old phase must not survive into the new group
    Bot ti = Entered(t);
    ti.complete = true;
    ti.Tick(t + 1000);
    Require(ti.makeTurnIn == 1 && FollowPhase(ti.followPhase) == FollowPhase::TurnIn, "c complete: TurnIn");
    ti.complete = false;   // c' of the new group is open
    ti.Mail(Directive(2, gm::Verb::Enter, 9, 600));
    ti.Tick(t + 2000);
    Require(IsAck(ti.acks.back(), mi::kAckApplied, kNone) && ti.groupSerial == 9 && ti.undo == 1 && ti.roleByHandler &&
        FollowPhase(ti.followPhase) == FollowPhase::Follow && ti.makeFollower == 2,
        "move out of TurnIn: the follow undone and started again for the new group");
    // move follower -> follower with a far new leader: no longer following the old leader, the bot searches c' itself
    Bot nf = Entered(t);
    nf.coreLeader = 14;
    nf.leaderNear = false;
    nf.Mail(Directive(2, gm::Verb::Enter, 9, 600));
    nf.Tick(t + 2000);
    Require(IsAck(nf.acks.back(), mi::kAckApplied, kNone) && nf.leaderLow == 14 && nf.undo == 1 && nf.roleByHandler &&
        nf.makeFollower == 1 && nf.N(GrpField::FollowSelf) == 1 && nf.N(GrpField::LeaderFar) == 0,
        "move to a far new leader: the old follow undone, follow_self");
    // move follower -> follower, the same leader still followed: the follow set stands
    Bot sl = Entered(t);
    sl.Mail(Directive(2, gm::Verb::Enter, 9, 600));
    sl.Tick(t + 2000);
    Require(IsAck(sl.acks.back(), mi::kAckApplied, kNone) && sl.groupSerial == 9 && sl.undo == 0 && sl.roleByHandler &&
        sl.makeFollower == 1, "move with the same leader: no undo, no second follow set");
    // move refused: leave the old group
    Bot r = Entered(t);
    r.inLog = false;
    r.Mail(Directive(2, gm::Verb::Enter, 9, 600));
    r.Tick(t + 2000);
    Require(IsAck(r.acks.back(), mi::kAckRefused, gm::kAckReasonGoalNotInLog) && r.mode == Mode::Quest && !r.groupHeld && r.undo == 1,
        "move refused: leave, QUEST, 3/24");
    Require(r.N(GrpField::EnterRef) == 1 && r.N(GrpField::R24) == 1, "counted enter_ref and r24");
    // the same serial again: idempotent
    Bot i = Entered(t);
    i.Mail(Directive(5, gm::Verb::Enter, 7, 500));
    i.Tick(t + 3000);
    Require(IsAck(i.acks.back(), mi::kAckApplied, kNone) && i.groupSerial == 7 && i.makeFollower == 1 && i.undo == 0,
        "Enter of the same serial: 2/0, nothing changes");
}

void TestRefusals()
{
    Bot b;
    b.arbiter = 1;
    b.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    b.Tick(1000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonArbiterOff) && b.N(GrpField::R22) == 1 && b.mode == Mode::Quest,
        "Arbiter != 2: 3/22");
    b.arbiter = 2;
    b.mode = Mode::Rescue;
    b.Mail(Directive(2, gm::Verb::Enter, 7, 500));
    b.Tick(2000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, std::uint8_t(Deny::RescueFirst)) && b.N(GrpField::RDeny) == 1, "RESCUE: 3/7");
    b.mode = Mode::Passive;
    b.Mail(Directive(3, gm::Verb::Enter, 7, 500));
    b.Tick(3000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, std::uint8_t(Deny::PassiveHold)) && b.N(GrpField::RDeny) == 2, "PASSIVE: 3/4");
    b.mode = Mode::Quest;
    b.role = 1;   // S7 or BOT-12
    b.Mail(Directive(4, gm::Verb::Enter, 7, 500));
    b.Tick(4000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonLegacyGroupRole) && b.N(GrpField::R21) == 1, "a legacy role: 3/21");
    b.role = 0;
    b.startZoneHold = true;
    b.Mail(Directive(5, gm::Verb::Enter, 7, 500));
    b.Tick(5000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonStartZoneHold) && b.N(GrpField::R16) == 1, "start zone: 3/16");
    b.startZoneHold = false;
    b.focus = 99;
    b.Mail(Directive(6, gm::Verb::Enter, 7, 500));
    b.Tick(6000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonFocusConflict) && b.N(GrpField::R25) == 1, "focus: 3/25");
    b.focus = 0;
    b.Mail(Directive(7, gm::Verb::Enter, 7, 500, gm::GroupType::Dungeon));
    b.Tick(7000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, std::uint8_t(Deny::NotAMode)), "a dungeon type: 3/2");
    b.writeOk = false;
    b.Mail(Directive(8, gm::Verb::Enter, 7, 500));
    b.Tick(8000);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonLaneBusy) && b.mode == Mode::Quest && !b.groupHeld &&
        b.N(GrpField::R23) == 1, "a failed own write: rolled back, 3/23");
    b.writeOk = true;
    // the verbs 0 and 4-15: 3/2, dir_bad and bad_ref, nothing changes
    for (std::uint32_t v = 4; v < 16; ++v)
    {
        gm::DirectiveBits d;
        d.verb = gm::Verb(v);
        d.serial = 7;
        b.Mail(gm::MakeDirective(100 + v, d, 500, 1, 0.0f, 0.0f, 0));
        b.Tick(9000 + v);
    }
    gm::DirectiveBits none;
    none.serial = 7;
    b.Mail(gm::MakeDirective(200, none, 500, 1, 0.0f, 0.0f, 0));
    b.Tick(9100);
    Require(b.N(GrpField::DirBad) == 13 && b.N(GrpField::BadRef) == 13 && b.mode == Mode::Quest, "13 bad verbs: 3/2 each");
    Require(IsAck(b.acks.back(), mi::kAckRefused, std::uint8_t(Deny::NotAMode)), "a bad verb: 3/2");
    // Change outside GROUP: 3/20; Exit outside GROUP: stale
    b.Mail(Directive(300, kChangeVerb, 7, 500));
    b.Tick(9200);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonNoOpenGroup) && b.N(GrpField::UpdateRef) == 1 &&
        b.N(GrpField::DirUpdate) == 1 && b.N(GrpField::R20) == 1, "a Change outside GROUP: 3/20");
    b.Mail(Directive(301, gm::Verb::Exit, 7, 500));
    b.Tick(9300);
    Require(IsAck(b.acks.back(), mi::kAckApplied, kNone) && b.N(GrpField::ExitStale) == 1, "an Exit outside GROUP: 2/0 stale");
    // the run equation holds over the whole sequence (single minute: nothing left open)
    Require(GrpBalanced(b.cnt, ArbiterGrpMinute()), "one ack per answered directive");
}

void TestChangeInGroup()
{
    std::uint32_t t = 0;
    Bot b = Entered(t);
    b.Mail(Directive(2, kChangeVerb, 7, 500, gm::GroupType::Quest, true, kGatherMeet));
    b.Tick(t + 100);
    Require(IsAck(b.acks.back(), mi::kAckApplied, kNone) && b.gatherFlag && b.gatherKind == kGatherMeet && b.N(GrpField::UpdateOk) == 1,
        "a Change in GROUP of the same serial: 2/0, the new kind");
    b.Mail(Directive(3, kChangeVerb, 8, 500));
    b.Tick(t + 200);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonNoOpenGroup), "a Change of another serial: 3/20");
}

void TestRescue()
{
    std::uint32_t t = 0;
    // death in GROUP, back with the view true
    Bot b = Entered(t);
    b.alive = false;
    b.Tick(t + 100);
    Require(b.mode == Mode::Rescue && b.rescueByGroup && b.N(GrpField::RescueIn) == 1 && b.role == 1 && b.roleByHandler,
        "death in GROUP: RESCUE, role and follow kept");
    Require(b.orphanNextAskMs == 0 && b.orphanFalseSinceMs == 0, "the orphan clock is reset at the RESCUE entry");
    b.Mail(Directive(2, gm::Verb::Enter, 9, 600));
    b.Tick(t + 200);
    Require(IsAck(b.acks.back(), mi::kAckRefused, std::uint8_t(Deny::RescueFirst)) && b.groupSerial == 7, "Enter in RESCUE: 3/7");
    b.Mail(Directive(3, kChangeVerb, 7, 500));
    b.Tick(t + 300);
    Require(IsAck(b.acks.back(), mi::kAckRefused, gm::kAckReasonNoOpenGroup), "Change in RESCUE: 3/20");
    b.live = LiveClass::QuestOther;   // a quest target != c made while dead
    b.intact = false;
    b.alive = true;
    int const asks = b.asks;
    b.Tick(t + 400);
    Require(b.mode == Mode::Group && b.N(GrpField::RescueGroup) == 1 && b.asks == asks + 1, "alive, view true: back to GROUP");
    Require(b.N(GrpField::GoalClose) == 1 && b.live == LiveClass::None, "R-c at the return: the target != c closed");
    Require(b.makeFollower == 2 && b.adoptDue, "the follower check at once; the adoption is due");
    b.Tick(t + 1400);
    Require(b.asks == asks + 1, "no second view question in the pass after the return (the fresh one counts)");
    b.Tick(t + 400 + kOrphanAskMs);
    Require(b.asks == asks + 2, "the next view question 10 s after the fresh one");

    // death in GROUP, back with the view false: rescue_back
    Bot c = Entered(t);
    c.alive = false;
    c.Tick(t + 100);
    c.managed = false;
    c.alive = true;
    c.Tick(t + 200);
    Require(c.mode == Mode::Quest && !c.groupHeld && c.undo == 1 && c.N(GrpField::RescueBack) == 1, "view false: QUEST, rescue_back");

    // Exit while dead, then revive: QUEST, and the next Enter gets 2/0 (not 3/7)
    Bot e = Entered(t);
    e.alive = false;
    e.Tick(t + 100);
    e.Mail(Directive(2, gm::Verb::Exit, 7, 500));
    e.Tick(t + 200);
    Require(IsAck(e.acks.back(), mi::kAckApplied, kNone) && e.N(GrpField::ExitOk) == 1 && !e.groupHeld && e.mode == Mode::Rescue &&
        e.undo == 1, "Exit while dead: 2/0, group forgotten, no Decide while dead");
    e.alive = true;
    e.Tick(t + 300);
    Require(e.mode == Mode::Quest && !e.rescueByGroup && e.N(GrpField::RescueBack) == 1, "revived without a group: QUEST");
    e.Mail(Directive(3, gm::Verb::Enter, 8, 500));
    e.Tick(t + 400);
    Require(IsAck(e.acks.back(), mi::kAckApplied, kNone) && e.mode == Mode::Group, "the next Enter: 2/0");

    // orphan while dead: forget only; revive -> QUEST
    Bot o = Entered(t);
    o.alive = false;
    o.Tick(t + 100);
    o.managed = false;
    for (std::uint32_t s = 1; s <= 80; ++s)
        o.Tick(t + 100 + s * 1000);
    Require(o.N(GrpField::Orphan) == 1 && !o.groupHeld && o.mode == Mode::Rescue, "orphan while dead: the group forgotten");
    o.alive = true;
    o.Tick(t + 90000);
    Require(o.mode == Mode::Quest && o.N(GrpField::RescueBack) == 1, "revived: QUEST");

    // arbiter 2 -> 1 while dead: the group forgotten; the disarm edge sets PASSIVE; the flag is false at the next tick; a later
    // foreign RESCUE is not ended at the revival
    Bot a = Entered(t);
    a.alive = false;
    a.Tick(t + 100);
    a.arbiter = 1;
    a.Tick(t + 200);
    Require(!a.groupHeld && a.mode == Mode::Rescue && a.undo == 1 && a.lefts.empty(), "arbiter 2 -> 1 while dead: forgotten, no left");
    a.mode = Mode::Passive;   // DisarmEnforce: RESCUE -> PASSIVE (External)
    a.Tick(t + 300);
    Require(!a.rescueByGroup, "the flag is false at the next tick");
    a.arbiter = 2;
    a.mode = Mode::Rescue;    // a foreign RESCUE (funnel), not this handler's
    a.alive = true;
    a.Tick(t + 400);
    Require(a.mode == Mode::Rescue && a.N(GrpField::RescueBack) == 0, "a foreign RESCUE is not ended at the revival");
}

void TestGoalEndAndLeft()
{
    std::uint32_t t = 0;
    // done: leave first, then left 26, lock; the Exit after it is stale and ends the lock
    Bot d = Entered(t);
    d.turnedIn = true;
    d.Tick(t + 1000);
    Require(d.mode == Mode::Quest && d.lefts.size() == 1 && d.lefts[0] == gm::kAckReasonGoalDone && d.modeAtLeft[0] == Mode::Quest,
        "goal done: GROUP left before the left 26");
    Require(d.selfExitSerial == 7 && d.N(GrpField::GoalDone) == 1 && d.N(GrpField::Left) == 1, "the self-exit lock with the serial");
    d.Mail(Directive(2, gm::Verb::Exit, 7, 500));
    d.Tick(t + 2000);
    Require(d.N(GrpField::ExitStale) == 1 && d.selfExitSerial == 0, "the Exit after the left: stale, the lock ends");
    // lost: 27
    Bot l = Entered(t);
    l.inLog = false;
    l.Tick(t + 1000);
    Require(l.lefts.size() == 1 && l.lefts[0] == gm::kAckReasonGoalLost && l.N(GrpField::GoalLost) == 1, "goal lost: left 27");
    // the lock ends by itself after kOrphanGraceMs
    l.Tick(t + 1000 + gm::kOrphanGraceMs);
    Require(l.selfExitSerial == 0, "the lock ends after the grace without an Exit");
    // a failed left, then an Exit of the same serial in the next tick: left_cancel, no retry
    Bot c = Entered(t);
    c.postOk = false;
    c.turnedIn = true;
    c.Tick(t + 1000);
    Require(c.lefts.empty() && c.leftRetrySerial == 7, "the left post failed: retry armed");
    c.postOk = true;
    c.Mail(Directive(2, gm::Verb::Exit, 7, 500));
    c.Tick(t + 1100);
    Require(c.N(GrpField::LeftCancel) == 1 && c.lefts.empty() && c.N(GrpField::Left) == 0 && c.leftRetrySerial == 0,
        "the Exit cancels the retry");
    // a failed left, retried in the next tick: sent, or dropped on a second false
    Bot r = Entered(t);
    r.postOk = false;
    r.turnedIn = true;
    r.Tick(t + 1000);
    r.postOk = true;
    r.Tick(t + 1100);
    Require(r.N(GrpField::Left) == 1 && r.lefts.size() == 1 && r.leftRetrySerial == 0, "the retry posts once: left");
    Bot x = Entered(t);
    x.postOk = false;
    x.turnedIn = true;
    x.Tick(t + 1000);
    x.Tick(t + 1100);
    Require(x.N(GrpField::LeftDrop) == 1 && x.N(GrpField::Left) == 0 && x.leftRetrySerial == 0, "a second false: left_drop");
    x.Tick(t + 1200);
    Require(x.N(GrpField::LeftDrop) == 1, "no third try");
}

void TestAdoptAndClose()
{
    Bot b;
    b.coreLeader = 11;   // the leader
    b.live = LiveClass::QuestGoal;   // an open target of c from before the Enter
    b.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    b.Tick(1000);   // the Enter (step 4) sets adoptDue, the 1/s check of the same tick (step 6) adopts
    Require(b.N(GrpField::GoalClose) == 0 && b.live == LiveClass::QuestGoal, "R-c keeps a target of c");
    Require(b.N(GrpField::GoalAdoptEnter) == 1 && b.groupOwns && !b.adoptDue, "the first check adopts it once: goal_adopt_enter");
    b.Tick(1500);
    Require(b.N(GrpField::GoalAdoptEnter) == 1 && b.N(GrpField::CrossKeep) == 0, "held by Group: nothing more");
    b.groupOwns = false;   // a foreign write on c
    b.Tick(2000);
    Require(b.N(GrpField::CrossKeep) == 1 && b.N(GrpField::GoalAdoptEnter) == 1, "later: cross_keep");
    // a PREPARE at the Enter is cancelled, a quest target != c expires
    Bot p;
    p.coreLeader = 11;
    p.live = LiveClass::Prepare;
    p.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    p.Tick(1000);
    Require(p.N(GrpField::PrepCancel) == 1 && p.N(GrpField::GoalClose) == 0, "PREPARE at the Enter: prep_cancel");
    // a writer that sets a target != c again and again: goal_close each second, one close_burst edge, one close_warn edge
    for (std::uint32_t s = 1; s <= 40; ++s)
    {
        p.live = LiveClass::QuestOther;
        p.Tick(1000 + s * 1000);
    }
    Require(p.N(GrpField::GoalClose) == 40 && p.N(GrpField::CloseWarn) == 1, "the close loop: goal_close per second, warn once");
    Require(p.N(GrpField::CloseBurst) == 1, "41 closes in 41 s cross 30 in 60 s once: one close_burst");
}

void TestAckDrop()
{
    Bot b;
    b.coreLeader = 13;
    b.postOk = false;
    b.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    b.Tick(1000);
    Require(b.N(GrpField::EnterOk) == 1 && b.N(GrpField::AckDropEnter) == 1 && b.mode == Mode::Group, "a lost Enter ack: answered, ack_drop_enter");
    b.Mail(Directive(2, gm::Verb::Exit, 7, 500));
    b.Tick(1100);
    Require(b.N(GrpField::ExitOk) == 1 && b.N(GrpField::AckDropExit) == 1 && b.N(GrpField::AckDropEnter) == 1, "a lost Exit ack: ack_drop_exit");
    Require(b.N(GrpField::AckDropEnter) <= b.N(GrpField::EnterOk) + b.N(GrpField::EnterRef) + b.N(GrpField::UpdateOk) +
        b.N(GrpField::UpdateRef) + b.N(GrpField::BadRef) && b.N(GrpField::AckDropExit) <= b.N(GrpField::ExitOk) + b.N(GrpField::ExitStale),
        "the drops are subsets of their answers");
    Require(GrpBalanced(b.cnt, ArbiterGrpMinute()), "a dropped ack still counts as answered");
}

void TestReleaseAndHandler()
{
    // GroupRelease without a group: no effect
    Bot q;
    q.live = LiveClass::NonQuest;
    q.Release();
    Require(q.mode == Mode::Quest && q.live == LiveClass::NonQuest && q.modeLines == 0 && q.undo == 0, "Release without a group: nothing");
    std::uint32_t t = 0;
    Bot g = Entered(t);
    g.Release();
    Require(!g.groupHeld && g.undo == 1 && g.modeLines == 1 && g.lefts.empty(), "Release with a group: leave, no left");

    // GroupHandlerOn, both bodies
    for (std::uint32_t guid = 0; guid < 8; ++guid)
        Require(HandlerOnA(guid), "Q4 (a): always true");
    g_handlerKey = 0;
    Require(!HandlerOnB(1) && !HandlerOnB(2), "Q4 (b)/(c) key 0: off");
    g_handlerKey = 1;
    Require(HandlerOnB(1) && HandlerOnB(2), "key 1: all");
    g_handlerKey = 2;
    Require(HandlerOnB(1) && !HandlerOnB(2), "key 2: cohort B (odd)");
    g_handlerKey = 3;
    Require(!HandlerOnB(1) && HandlerOnB(2), "key 3: cohort A (even)");
    // the off path (only reachable under Q4 (b)/(c)): Enter 3/22; a held group is left silently, counted handler_off
    g_handlerKey = 0;
    Bot h;
    h.handlerOn = HandlerOnB;
    h.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    h.Tick(1000);
    Require(IsAck(h.acks.back(), mi::kAckRefused, gm::kAckReasonArbiterOff) && h.mode == Mode::Quest, "handler off: Enter 3/22");
    g_handlerKey = 1;
    Bot k;
    k.coreLeader = 13;
    k.handlerOn = HandlerOnB;
    k.Mail(Directive(1, gm::Verb::Enter, 7, 500));
    k.Tick(1000);
    Require(k.mode == Mode::Group, "handler on: GROUP");
    g_handlerKey = 0;
    k.Tick(1100);
    Require(k.mode == Mode::Quest && !k.groupHeld && k.undo == 1 && k.N(GrpField::HandlerOff) == 1 && k.lefts.empty(),
        "handler switched off: leave silently, handler_off, no left");
    g_handlerKey = 0;
}

void TestEarlyReturn()
{
    // QUEST without a directive, a retry or a lock: GroupTick returns at once and changes nothing
    Bot b;
    b.alive = false;
    b.Tick(1000);
    Require(b.mode == Mode::Quest && b.N(GrpField::RescueIn) == 0 && b.asks == 0, "no group: nothing");
    // a foreign RESCUE never set by the handler is not ended
    b.mode = Mode::Rescue;
    b.alive = true;
    b.Tick(2000);
    Require(b.mode == Mode::Rescue, "a foreign RESCUE stays");
}
}

int main()
{
    TestClassifyVerb();
    TestNewestDirective();
    TestEnterOrder();
    TestChangeExit();
    TestAckFields();
    TestOrphanClock();
    TestGoalEnd();
    TestLiveClass();
    TestChooseMove();
    TestFollowStep();
    TestCloseRing();
    TestSelfExitAndLeft();
    TestGrpBalanced();
    TestEnterFollowerAndLeader();
    TestFollowerStates();
    TestUndoPaths();
    TestRefusals();
    TestChangeInGroup();
    TestRescue();
    TestGoalEndAndLeft();
    TestAdoptAndClose();
    TestAckDrop();
    TestReleaseAndHandler();
    TestEarlyReturn();
    std::cout << "supervisor_group_policy_tests: OK\n";
    return 0;
}
