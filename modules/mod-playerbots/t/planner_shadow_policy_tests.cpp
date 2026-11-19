// A3 step 3 planner P-2a (A3-S3-PLANER-ENTWURF.md v0.3, frozen d6e51878; AiPlayerbot.Planner, default 0): the pure rules of the
// planner in the supervisor tick (PlannerShadowPolicy.h) - events by snapshot comparison, tiers, hysteresis, maximum holding time,
// the re-take rule of repeatable quests, the zone floor and the way back to the band, the WORK lock, the shadow comparison, W8 and
// the target memory, the minute line. The header names no game type, so this suite compiles it standalone. Nothing in the game
// calls it yet (P-2b wires it); t/contracts.d/a3s3_planner_shadow_policy_source_contract_tests.cmake pins the pure header.

#include "PlannerShadowPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

using namespace ai::planner;

Snapshot Base()
{
    Snapshot s;
    s.valid = true;
    s.atMs = 10000;
    s.level = 5;
    s.map = 0;
    s.zone = 85;
    s.x = 2000.0f;
    s.y = 1400.0f;
    s.runSpeed = 7.0f;
    s.quests[0].id = 100;
    s.quests[0].state = QuestState::Incomplete;
    s.quests[1].id = 101;
    s.quests[1].state = QuestState::Incomplete;
    return s;
}

void TestDiff()
{
    GoneList gone;
    Snapshot none;
    Snapshot a = Base();
    Require(Diff(none, a, gone, 60.0f, 1.5f) == EvStart, "a first snapshot is start");
    Require(Diff(a, none, gone, 60.0f, 1.5f) == EvNone, "an invalid snapshot gives nothing");
    Require(Diff(a, a, gone, 60.0f, 1.5f) == EvNone && gone.n == 0, "no change, no event");

    Snapshot b = a;
    b.level = 6;
    b.quests[0].state = QuestState::Complete;
    b.quests[1].id = 0;
    b.quests[1].state = QuestState::None;
    b.quests[2].id = 102;
    b.quests[2].state = QuestState::Incomplete;
    std::uint32_t ev = Diff(a, b, gone, 60.0f, 1.5f);
    Require((ev & EvLevel) && (ev & EvQuestComplete) && (ev & EvQuestAccept), "level, complete and accept");
    Require(gone.n == 1 && gone.ids[0] == 101, "the gone quest is listed for the label");
    Require(!(ev & (EvQuestReward | EvQuestDrop | EvQuestGone)), "Diff leaves the gone label to the adapter");
    Require(LabelGone(false, true, false) == EvQuestReward && LabelGone(false, false, false) == EvQuestDrop, "reward or drop");
    Require(LabelGone(true, true, false) == EvQuestGone && LabelGone(true, false, false) == EvQuestGone, "a repeatable quest is gone");
    Require(LabelGone(true, false, true) == EvQuestReward && LabelGone(false, false, true) == EvQuestReward, "the reward note makes it a reward (B2)");

    Snapshot c = a;
    c.quests[0].state = QuestState::Failed;
    Require(Diff(a, c, gone, 60.0f, 1.5f) == EvQuestFail, "fail");

    Snapshot d = a;
    d.alive = false;
    Require(Diff(a, d, gone, 60.0f, 1.5f) == EvDied, "died");
    Require(Diff(d, a, gone, 60.0f, 1.5f) == EvAlive, "alive");

    Snapshot e = a;
    e.zone = 130;
    Require(Diff(a, e, gone, 60.0f, 1.5f) == EvZone, "zone");
    e.map = 1;
    Require(Diff(a, e, gone, 60.0f, 1.5f) == EvMap, "a map change is map, not zone");

    Snapshot f = a;
    f.mode = 1;
    f.lane = 2;
    ev = Diff(a, f, gone, 60.0f, 1.5f);
    Require((ev & EvMode) && (ev & EvLane), "mode and lane edges");

    Snapshot t1 = a;
    t1.travel = TravelPhase::Travel;
    t1.travelDest = 9;
    Snapshot t2 = t1;
    t2.travel = TravelPhase::Work;
    Require(Diff(t1, t2, gone, 60.0f, 1.5f) == EvArrive, "TRAVEL -> WORK on the same destination is arrive");
    t2.travel = TravelPhase::Cooldown;
    Require(Diff(t1, t2, gone, 60.0f, 1.5f) == EvArrive, "TRAVEL -> COOLDOWN is arrive");
    t2.travelDest = 10;
    Require(Diff(t1, t2, gone, 60.0f, 1.5f) == EvNone, "another destination is no arrive");
}

void TestRelocate()
{
    Snapshot a = Base();
    Snapshot b = a;
    b.atMs = a.atMs + 1000;
    b.x = a.x + 50.0f;
    Require(!Relocated(a, b, 60.0f, 1.5f), "50 yd in 1 s is under the 60 yd floor");
    b.x = a.x + 70.0f;
    Require(Relocated(a, b, 60.0f, 1.5f), "70 yd in 1 s is a jump");
    b.atMs = a.atMs + 10000;
    Require(!Relocated(a, b, 60.0f, 1.5f), "70 yd in 10 s is a walk (7 x 10 x 1.5 = 105)");
    b.x = a.x + 1500.0f;
    Require(Relocated(a, b, 60.0f, 1.5f), "1500 yd in 10 s is a jump (Sepulcher case)");
    b.onTransport = true;
    Require(!Relocated(a, b, 60.0f, 1.5f), "never on a taxi or transport");
    b.onTransport = false;
    b.map = 1;
    Require(!Relocated(a, b, 60.0f, 1.5f), "only on the same map (OB-10 N3)");
    b.map = 0;
    GoneList gone;
    Require(Diff(a, b, gone, 60.0f, 1.5f) == EvRelocate, "Diff reports the jump");
    Snapshot back = b;
    back.atMs = a.atMs;   // a clock that went back: dt 0, the floor applies
    Require(Relocated(back, a, 60.0f, 1.5f), "no negative time");
}

void TestPlanDue()
{
    Require(PlanDue(EvZone, true, 1000, 0, 300000), "an event plans");
    Require(!PlanDue(EvDied, false, 1000, 0, 300000), "a death waits for the alive edge");
    Require(!PlanDue(EvDied, true, 1000, 0, 300000), "a death alone does not plan");
    Require(PlanDue(EvAlive, true, 1000, 0, 300000), "alive plans");
    Require(!PlanDue(EvNone, true, 299999, 0, 300000) && PlanDue(EvNone, true, 300000, 0, 300000), "the timer");
    Require(!PlanDue(EvNone, true, 900000, 0, 0), "timer 0 is off");
    Require(std::string(EvName(EvZone | EvLevel)) == "zone" && std::string(EvName(EvStart | EvZone)) == "start", "ev order");
    Require(std::string(EvName(EvTimer)) == "timer" && std::string(EvName(EvNone)) == "-", "ev names");
}

void TestTiers()
{
    TierInput in;
    in.bandTable = true;
    in.r1Allows = true;
    in.nextZoneKnown = true;
    in.eligibleA = 3;
    Require(PickTier(in).tier == Tier::A && PickTier(in).fb == Fallback::None, "A while there is work");
    in.eligibleA = 0;
    in.waitingA = 1;
    Require(PickTier(in).tier == Tier::B, "B while A is only held back");
    in.waitingA = 0;
    Require(PickTier(in).tier == Tier::C, "C when the zone is done");
    in.nextZoneKnown = false;
    Require(PickTier(in).tier == Tier::B && PickTier(in).fb == Fallback::None, "no next zone: grind, never wait");
    in.nextZoneKnown = true;
    in.r1Allows = false;
    Require(PickTier(in).tier == Tier::B && PickTier(in).fb == Fallback::R1Hold, "below R1: grind with r1_hold");
    in.bandTable = false;
    Require(PickTier(in).tier == Tier::B && PickTier(in).fb == Fallback::BandTableOff, "no table: grind with band_table_off");
    in.eligibleA = 2;
    Require(PickTier(in).tier == Tier::A && PickTier(in).fb == Fallback::BandTableOff, "no table: A in the current zone");
    Require(std::string(TierName(Tier::C)) == "C" && std::string(FallbackName(Fallback::Arbiter)) == "arbiter", "names");
}

void TestHysteresis()
{
    Hysteresis h;
    Incumbent inc;
    Require(HoldDecision(inc, 5, 10.0f, false, 1000, h) == Hold::Switch, "no incumbent: switch");
    inc.has = true;
    inc.stillEligible = true;
    inc.key = 5;
    inc.value = 10.0f;
    inc.sinceMs = 0;
    Require(HoldDecision(inc, 5, 99.0f, false, 1000, h) == Hold::Same, "the same target");
    Require(HoldDecision(inc, 6, 99.0f, false, 119999, h) == Hold::Bind, "inside the bind time");
    Require(HoldDecision(inc, 6, 11.4f, false, 120000, h) == Hold::Margin, "under 10 x 1.15 is not enough");
    Require(HoldDecision(inc, 6, 11.6f, false, 120000, h) == Hold::Switch, "beats the margin");
    Require(HoldDecision(inc, 6, 1.0f, true, 1000, h) == Hold::Forced, "forced breaks margin and bind");
    inc.stillEligible = false;
    Require(HoldDecision(inc, 6, 1.0f, false, 1000, h) == Hold::Switch, "a blocked incumbent goes");
    inc.stillEligible = true;
    Require(CannotWin(11.4f, inc, h) && !CannotWin(11.6f, inc, h), "pre-check without danger");
    Incumbent none;
    Require(!CannotWin(0.0f, none, h), "no incumbent: every candidate can win");
    Require(TargetKey(1, 100, 7) == TargetKey(1, 100, 7) && TargetKey(1, 100, 7) != TargetKey(0, 100, 7), "target key");
    Require(TargetKey(0, 0, 0) != 0, "a key is never 0");
    Require(std::string(HoldName(Hold::Bind)) == "bind", "hold name");
}

void TestMaxHold()
{
    Require(!MaxHoldDue(1000, 1000 + 1799999, 1800000, true), "under 1800 s");
    Require(MaxHoldDue(1000, 1000 + 1800000, 1800000, true), "at 1800 s");
    Require(!MaxHoldDue(1000, 1000 + 9000000, 1800000, false), "no routable turn-in, no max hold");
    Require(!MaxHoldDue(0, 9000000, 1800000, true), "no complete time");
    Require(!MaxHoldDue(1000, 9000000, 0, true), "0 is off");
}

void TestRepeatable()
{
    RepeatEntry log[kRepeatSlots];
    std::uint32_t const retake = 1800000;
    Require(!RepeatBlocked(FindRepeat(log, kRepeatSlots, 889), 1000, retake, 1), "never seen: free");
    NoteRepeatAccept(log, kRepeatSlots, 889, 1000);
    Require(RepeatBlocked(FindRepeat(log, kRepeatSlots, 889), 2000, retake, 1), "one take per hour");
    Require(!RepeatBlocked(FindRepeat(log, kRepeatSlots, 889), 2000, retake, 2), "two per hour allowed");
    NoteRepeatReward(log, kRepeatSlots, 889, 5000);
    Require(RepeatBlocked(FindRepeat(log, kRepeatSlots, 889), 5000 + retake - 1, retake, 0), "inside the re-take time");
    Require(!RepeatBlocked(FindRepeat(log, kRepeatSlots, 889), 5000 + retake, retake, 0), "after the re-take time");
    Require(!RepeatBlocked(FindRepeat(log, kRepeatSlots, 889), 1000 + kHourMs + retake, retake, 1), "a new hour");
    NoteRepeatAccept(log, kRepeatSlots, 889, 1000 + kHourMs);
    Require(FindRepeat(log, kRepeatSlots, 889)->takesThisHour == 1, "the hour starts again");
    for (std::uint32_t q = 1; q <= kRepeatSlots; ++q)
        NoteRepeatAccept(log, kRepeatSlots, 5000 + q, 4000000 + q);
    Require(!FindRepeat(log, kRepeatSlots, 889), "the least recently used entry goes");
}

void TestFloor()
{
    Require(FloorCheck(0, 5) == FloorVerdict::Unknown, "no row: unknown");
    Require(FloorCheck(10, 5) == FloorVerdict::Below, "L5 in a L10 zone");
    Require(FloorCheck(10, 10) == FloorVerdict::Ok, "at the floor");
    Require(std::string(FloorName(FloorVerdict::Below)) == "below", "floor name");
    Require(BackToBandDue(true, 0, FloorVerdict::Below, false, true), "the Sepulcher case");
    Require(BackToBandDue(true, 0, FloorVerdict::Ok, false, true), "outside the plan zone");
    Require(!BackToBandDue(true, 0, FloorVerdict::Ok, true, true), "in the plan zone");
    Require(!BackToBandDue(true, 1, FloorVerdict::Below, false, true), "a quest in the log");
    Require(!BackToBandDue(false, 0, FloorVerdict::Below, false, true), "a destination was found");
    Require(!BackToBandDue(true, 0, FloorVerdict::Below, false, false), "no plan zone to go back to");
    Require(BackToBandAllowed(0, 5, 120000) && !BackToBandAllowed(1000, 120999, 120000) && BackToBandAllowed(1000, 121000, 120000),
        "one back_to_band per bind time");
}

void TestExpireAndCompare()
{
    Require(ExpireCheck(TravelPhase::Cooldown, true) == ExpireGate::Cooldown, "COOLDOWN is always locked");
    Require(ExpireCheck(TravelPhase::Work, false) == ExpireGate::Work, "WORK is locked");
    Require(ExpireCheck(TravelPhase::Work, true) == ExpireGate::WorkOverride, "max_hold / back_to_band break WORK");
    Require(ExpireCheck(TravelPhase::Travel, false) == ExpireGate::Allowed, "TRAVEL may expire");

    Require(ComparePick(false, true, 0, 10, 120000, 1, 2, 1, 2) == Compare::NaPurpose, "another purpose");
    Require(ComparePick(true, false, 0, 10, 120000, 1, 2, 1, 2) == Compare::NaStale, "no pick");
    Require(ComparePick(true, true, 0, 120000, 120000, 1, 2, 1, 2) == Compare::NaStale, "an old pick");
    Require(ComparePick(true, true, 0, 10, 120000, 1, 2, 1, 2) == Compare::Agree, "agree");
    Require(ComparePick(true, true, 0, 10, 120000, 1, 2, 1, 3) == Compare::Disagree, "another place");
    Require(ComparePick(true, true, 0, 10, 120000, 1, 2, 4, 2) == Compare::Disagree, "another quest");
}

void TestDeadAndMemory()
{
    Require(DeadWaitMs(300, 600) == 600000 && DeadWaitMs(120, 600) == 240000 && DeadWaitMs(0, 600) == 0, "wait time");
    Require(DeadProbe(1, 0, 5000, 1000) == DeadStep::Alive, "a living target");
    Require(DeadProbe(0, 1000, 1999, 1000) == DeadStep::Wait && DeadProbe(0, 1000, 2000, 1000) == DeadStep::Close, "wait, close");

    MemorySlot mem[kMemorySlots];
    Require(!IsClosed(mem, kMemorySlots, 7, 0), "empty memory");
    Close(mem, kMemorySlots, 7, 100, 0, CloseReason::Dead, 1000, 5000);
    Require(IsClosed(mem, kMemorySlots, 7, 5999) && !IsClosed(mem, kMemorySlots, 7, 6000), "a close has an end");
    Require(mem[0].deadCycles == 1, "one dead cycle");
    Close(mem, kMemorySlots, 7, 100, 0, CloseReason::Dead, 7000, 5000);
    Require(mem[0].deadCycles == 2 && mem[1].key == 0, "the same key reuses its slot");
    Close(mem, kMemorySlots, 8, 101, 0, CloseReason::None, 7000, 5000);
    Close(mem, kMemorySlots, 9, 102, 0, CloseReason::R9, 7000, 0);
    Require(!IsClosed(mem, kMemorySlots, 8, 7001) && !IsClosed(mem, kMemorySlots, 9, 7001), "never a close without reason or end");
    for (std::uint32_t k = 20; k < 20 + kMemorySlots; ++k)
        Close(mem, kMemorySlots, k, k, 0, CloseReason::Suppressed, 8000 + k, 100000);
    Require(!IsClosed(mem, kMemorySlots, 7, 8100), "the oldest slot is replaced when none is expired");
    Require(std::string(CloseReasonName(CloseReason::R9)) == "r9", "close reason name");
}

void TestMinute()
{
    PlanMinCounts a;
    a.plans = 3;
    a.agree = 2;
    a.msMax = 4;
    a.backToBandWould = 1;
    PlanMinCounts b;
    b.plans = 1;
    b.msMax = 9;
    b.naPurpose = 5;
    Add(a, b);
    Require(a.plans == 4 && a.msMax == 9 && a.naPurpose == 5 && a.backToBandWould == 1, "sums and the max");
    char buf[1024];
    std::size_t const n = FormatPlanMin(buf, sizeof(buf), 'B', 42, a);
    std::string const line(buf);
    Require(n == line.size(), "length");
    Require(line.rfind("[PlanMin] cohort=B min=42 plans=4 keeps=0 switches=0 ", 0) == 0, "head");
    Require(line.find(" agree=2 disagree=0 ") != std::string::npos, "agree");
    Require(line.find(" back_to_band_would=1 back_to_band_repeat=0 expire_work_override_would=0 overwritten_shadow=0 "
        "na_purpose=5 na_stale=0") != std::string::npos && line.back() == '0', "tail");
    Require(line.size() < 900, "fits the buffer");
    char small[16];
    Require(FormatPlanMin(small, sizeof(small), 'A', 1, a) == sizeof(small) - 1, "cut at the buffer");
}
}

int main()
{
    TestDiff();
    TestRelocate();
    TestPlanDue();
    TestTiers();
    TestHysteresis();
    TestMaxHold();
    TestRepeatable();
    TestFloor();
    TestExpireAndCompare();
    TestDeadAndMemory();
    TestMinute();
    std::cout << "planner_shadow_policy_tests: OK\n";
    return 0;
}
