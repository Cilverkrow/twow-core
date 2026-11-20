// A3 step 3 planner P-2a (A3-S3-PLANER-ENTWURF.md v0.3, frozen d6e51878; AiPlayerbot.Planner, default 0): the pure rules of the
// planner in the supervisor tick (PlannerShadowPolicy.h) - events by snapshot comparison, tiers, hysteresis, maximum holding time,
// the re-take rule of repeatable quests, the zone floor and the way back to the band, the WORK lock, the shadow comparison, W8 and
// the target memory, the minute line. The header names no game type, so this suite compiles it standalone. Nothing in the game
// calls it yet (P-2b wires it); t/contracts.d/a3s3_planner_shadow_policy_source_contract_tests.cmake pins the pure header.

#include "PlannerShadowPolicy.h"
#include "PlannerShadow.h"   // P-2b: RewardNotes, UpdateComplete
#include "PlannerWrite.h"    // P-3a: the plan attempt's counter classes (header templates only)
#include "CohortPolicy.h"    // P-3a: WritesFor against the cohort parity rule

#include <atomic>
#include <cstdlib>
#include <cstring>
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
    b.passiveTicks = 7;
    b.agreeGrind = 1;
    b.backToBandWouldGrind = 1;
    Add(a, b);
    Require(a.plans == 4 && a.msMax == 9 && a.naPurpose == 5 && a.backToBandWould == 1, "sums and the max");
    char buf[1280];
    std::size_t const n = FormatPlanMin(buf, sizeof(buf), 'B', 42, a);
    std::string const line(buf);
    Require(n == line.size(), "length");
    Require(line.rfind("[PlanMin] cohort=B min=42 plans=4 keeps=0 switches=0 ", 0) == 0, "head");
    Require(line.find(" agree=2 disagree=0 ") != std::string::npos, "agree");
    Require(line.find(" back_to_band_would=1 back_to_band_repeat=0 expire_work_override_would=0 overwritten_shadow=0 "
        "na_purpose=5 na_stale=0") != std::string::npos && line.back() == '1', "tail");
    Require(line.find(" overwritten_neutral=0 switch_ms=0 passive_ticks=7 agree_grind=1 disagree_grind=0 back_to_band_would_quest=0 "
        "back_to_band_would_grind=1") != std::string::npos, "the P-2b fields at the end (B4, B6, B7)");
    Require(line.size() < 1000, "fits the buffer");
    char small[16];
    Require(FormatPlanMin(small, sizeof(small), 'A', 1, a) == sizeof(small) - 1, "cut at the buffer");
}

// P-2b (OB-10 B2): the planner's own reward note - no duplicates, the oldest drops when full, Take removes; and the complete marks
// keep the first time a quest turned complete.
void TestNotesAndComplete()
{
    RewardNotes notes;
    notes.Note(0);
    Require(notes.n == 0, "quest 0 is no note");
    notes.Note(10);
    notes.Note(10);
    Require(notes.n == 1, "no duplicate");
    notes.Note(11);
    notes.Note(12);
    notes.Note(13);
    notes.Note(14);
    Require(notes.n == RewardNotes::kSlots && !notes.Take(10) && notes.Take(14) && notes.n == 3, "full: the oldest drops");
    Require(!notes.Take(14) && notes.Take(11), "Take removes");
    notes.Clear();
    Require(notes.n == 0 && !notes.Take(12), "clear");

    CompleteMark marks[kQuestSlots];
    Snapshot s;
    s.valid = true;
    s.quests[0] = {5, QuestState::Complete};
    s.quests[1] = {6, QuestState::Incomplete};
    UpdateComplete(marks, s, 1000);
    Require(CompleteSince(marks, 5) == 1000 && CompleteSince(marks, 6) == 0, "a new complete quest gets now");
    UpdateComplete(marks, s, 5000);
    Require(CompleteSince(marks, 5) == 1000, "the first time stays");
    s.quests[0].state = QuestState::Incomplete;
    UpdateComplete(marks, s, 6000);
    Require(CompleteSince(marks, 5) == 0, "no longer complete: no mark");
    UpdateComplete(marks, s, 0);
    s.quests[1].state = QuestState::Complete;
    UpdateComplete(marks, s, 0);
    Require(CompleteSince(marks, 6) == 1, "time 0 is stored as 1");

    // overwritten_shadow (B1 proxy): only an open target, a questbook writer and inside the bind time.
    AgreeMark agree;
    agree.destKey = 77;
    agree.atMs = 1000;
    Snapshot prev;
    prev.valid = true;
    prev.travelDest = 77;
    prev.travel = TravelPhase::Travel;
    Snapshot next = prev;
    Require(CheckAgree(agree, prev, next, 2000, 120000, true) == AgreeEnd::Keep, "same target: keep");
    next.travelDest = 78;
    Require(CheckAgree(agree, prev, next, 2000, 120000, true) == AgreeEnd::Overwritten, "open target replaced by the lane");
    Require(CheckAgree(agree, prev, next, 2000, 120000, false) == AgreeEnd::Ended, "a group or neutral writer: no count");
    prev.travel = TravelPhase::Cooldown;
    Require(CheckAgree(agree, prev, next, 2000, 120000, true) == AgreeEnd::Ended, "a finished trip: succession");
    prev.travel = TravelPhase::Work;
    Require(CheckAgree(agree, prev, next, 121000, 120000, true) == AgreeEnd::Ended, "after the bind time: no count");
    Require(CheckAgree(AgreeMark(), prev, next, 2000, 120000, true) == AgreeEnd::Keep, "no mark");


    // OB-60 (P-2a pre-review): a quest that only leaves the log sets no bit in Diff; the adapter's label makes the plan due.
    Snapshot before;
    before.valid = true;
    before.atMs = 1000;
    before.quests[0] = {9, QuestState::Incomplete};
    Snapshot after = before;
    after.atMs = 2000;
    after.quests[0] = QuestSlot();
    GoneList gone;
    std::uint32_t ev = Diff(before, after, gone, 60.0f, 1.5f);
    Require(ev == EvNone && gone.n == 1 && gone.ids[0] == 9, "a gone quest alone sets no bit");
    Require(!PlanDue(ev, true, 2000, 1000, 300000), "no label: no plan");
    ev |= LabelGone(false, false, false);
    Require(PlanDue(ev, true, 2000, 1000, 300000), "the label makes the plan due");
    // W8: an unknown respawn waits the full maximum.
    Require(DeadWaitFor(0, 600) == 600000u && DeadWaitFor(60, 600) == 120000u && DeadWaitFor(500, 600) == 600000u, "dead wait");
}

// ---- A3 step 3 planner P-3a (P3-ENTWURF-v0.2, frozen d45c4961, Nachtraege 1-3; AiPlayerbot.Planner = 2, default 0) -----------
// Planner.WriteCohort: 0..3 kept, above 3 is 0 (never "all"); only Planner = 2 writes; the parity rule of CohortPolicy.h.
void TestWriteCohort()
{
    for (std::uint32_t raw = 0; raw <= kWriteCohortMax; ++raw)
        Require(WriteCohortOf(raw) == raw, "WriteCohort 0..3 kept");
    Require(WriteCohortOf(4) == 0 && WriteCohortOf(0xFFFFFFFFu) == 0, "WriteCohort above 3 is 0, never all");
    for (std::uint32_t guid = 0; guid < 8; ++guid)
    {
        for (std::uint32_t raw = 0; raw <= 5; ++raw)
        {
            std::uint32_t const mode = WriteCohortOf(raw);
            Require(WritesFor(2, mode, guid) == ai::cohort::Applies(mode, guid), "WritesFor is the cohort parity rule");
            Require(!WritesFor(0, mode, guid) && !WritesFor(1, mode, guid) && !WritesFor(3, mode, guid), "only Planner = 2 writes");
        }
        Require(!WritesFor(2, 0, guid), "Planner = 2 with WriteCohort 0 writes nothing");
        Require(WritesFor(2, 1, guid), "WriteCohort 1: every bot");
    }
    Require(WritesFor(2, 2, 7) && !WritesFor(2, 2, 8) && WritesFor(2, 3, 8) && !WritesFor(2, 3, 7), "2 = cohort B (odd), 3 = A");
}

ExpireFacts QuestFacts(TravelPhase phase)
{
    ExpireFacts f;
    f.phase = phase;
    f.liveNull = false;
    f.liveDestKey = 11;
    f.liveQuest = 100;
    f.pickDestKey = 22;
    f.pickQuest = 200;
    return f;
}

ExpireStep Decide2to5(ExpireFacts const& f, bool forced)
{
    return ExpireDecide(f, ExpireCheck(f.phase, forced));
}

// Section 2.3, conditions 2-7, P3-1 (grind by zone), P3-2 (group copy), OB-60 H1 (DestKey AND quest).
void TestExpireRules()
{
    ExpireFacts f = QuestFacts(TravelPhase::Travel);
    Require(Decide2to5(f, false) == ExpireStep::Go, "another quest target expires");
    f.pickDestKey = 11;
    f.pickQuest = 100;
    Require(Decide2to5(f, false) == ExpireStep::AppliedLive, "the live target is the pick: applied_live");
    f.pickQuest = 101;
    Require(Decide2to5(f, false) == ExpireStep::Go, "H1: the same destination with another quest is not the pick");
    f.pickQuest = 100;
    f.pickDestKey = 12;
    Require(Decide2to5(f, false) == ExpireStep::Go, "H1: the same quest at another destination is not the pick");

    Require(Decide2to5(QuestFacts(TravelPhase::None), false) == ExpireStep::NotOpen, "nothing open: nothing to expire");
    Require(Decide2to5(QuestFacts(TravelPhase::Expired), true) == ExpireStep::NotOpen, "an expired target is not open");
    ExpireFacts null = QuestFacts(TravelPhase::Travel);
    null.liveNull = true;
    Require(Decide2to5(null, true) == ExpireStep::NotOpen, "the null target is never expired");

    // P3-1: a grind pick (destKey 0) compares the live target's zone, never the key.
    ExpireFacts g = QuestFacts(TravelPhase::Travel);
    g.pickGrind = true;
    g.pickDestKey = 0;
    g.pickQuest = 0;
    g.pickZone = 40;
    g.liveZone = 40;
    Require(Decide2to5(g, false) == ExpireStep::AppliedLive, "P3-1: a mob target in the pick zone is the grind pick");
    g.liveZone = 0;
    Require(Decide2to5(g, true) == ExpireStep::ZoneUnknown, "P3-1: an unknown zone expires nothing");
    g.liveZone = 12;
    Require(Decide2to5(g, false) == ExpireStep::Go, "P3-1: a target in another zone expires");

    // Condition 3 (B3): COOLDOWN never, WORK only for a forced pick (max_hold; back_to_band never reaches the expire, V-1).
    Require(Decide2to5(QuestFacts(TravelPhase::Cooldown), true) == ExpireStep::DeniedCooldown, "COOLDOWN is never expired");
    Require(Decide2to5(QuestFacts(TravelPhase::Work), false) == ExpireStep::DeniedWork, "WORK without a forced pick");
    Require(ExpireCheck(TravelPhase::Work, true) == ExpireGate::WorkOverride &&
        Decide2to5(QuestFacts(TravelPhase::Work), true) == ExpireStep::Go, "WORK with a forced pick: expire_work_override");
    // Conditions 4 and 5.
    f = QuestFacts(TravelPhase::Travel);
    f.repeat = true;
    Require(Decide2to5(f, false) == ExpireStep::Repeat, "rule 4: once per pick");
    f.repeat = false;
    f.fallbackBlock = true;
    Require(Decide2to5(f, false) == ExpireStep::BlockedFb, "rule 5: blocked after a fallback");
    Require(FallbackBlocks(1000, 1000 + 119999, 120000) && !FallbackBlocks(1000, 1000 + 120000, 120000) &&
        !FallbackBlocks(0, 5000, 120000), "rule 5 lasts MinBindSec, no fallback blocks nothing");
    // Conditions 6 and 7 (P3-2): the lane first, then the group copy.
    Require(ExpireAdmit(false, true) == ExpireStep::DeniedLane && ExpireAdmit(true, true) == ExpireStep::DeniedGroup &&
        ExpireAdmit(true, false) == ExpireStep::Go, "P3-2: the lane, then the group copy");
}

// The pending expire: P3-4 (nothing expires by waiting) and F8 (i)-(iii).
void TestPendingExpire()
{
    for (ExpireStep s : {ExpireStep::DeniedWork, ExpireStep::BlockedFb, ExpireStep::DeniedLane, ExpireStep::DeniedGroup,
             ExpireStep::DeniedThread})
        Require(PendingStarts(s), "a denial by 3 (WORK), 5, 6, 7 or the thread leaves the expire pending");
    for (ExpireStep s : {ExpireStep::Go, ExpireStep::NotOpen, ExpireStep::AppliedLive, ExpireStep::ZoneUnknown,
             ExpireStep::DeniedCooldown, ExpireStep::Repeat, ExpireStep::Expired})
        Require(!PendingStarts(s), "no pending expire after these steps");
    // P3-4: WORK without a forced pick stays denied however long the pending expire waits.
    ExpireFacts f = QuestFacts(TravelPhase::Work);
    for (int tick = 0; tick < 1000; ++tick)
    {
        ExpireStep const s = Decide2to5(f, false);
        Require(s == ExpireStep::DeniedWork && PendingKeeps(7, 7, s), "P3-4: no expire by waiting");
    }
    // F8 (a): WORK -> TRAVEL on the same target fires in TRAVEL (expire_after_work).
    f.phase = TravelPhase::Travel;
    Require(Decide2to5(f, false) == ExpireStep::Go, "F8 (a): the pending expire fires in TRAVEL");
    // F8 (iii): WORK -> COOLDOWN ends it, as does the end of the target.
    f.phase = TravelPhase::Cooldown;
    ExpireStep s = Decide2to5(f, false);
    Require(s == ExpireStep::DeniedCooldown && !PendingKeeps(7, 7, s), "F8 (iii): WORK -> COOLDOWN ends the pending expire");
    f.phase = TravelPhase::None;
    s = Decide2to5(f, false);
    Require(s == ExpireStep::NotOpen && !PendingKeeps(7, 7, s), "the end of the target ends it");
    // F8 (i): only while its key is the pick.
    Require(!PendingKeeps(7, 8, ExpireStep::DeniedWork) && !PendingKeeps(0, 0, ExpireStep::DeniedWork), "F8 (i): a new pick ends it");
}

// P3-3 and the feed check (section 2.4).
void TestModeAndFeed()
{
    Require(ModeSwitchFrom(Hold::Switch) && ModeSwitchFrom(Hold::Forced), "a pick change may switch the mode");
    Require(!ModeSwitchFrom(Hold::Same) && !ModeSwitchFrom(Hold::Margin) && !ModeSwitchFrom(Hold::Bind),
        "P3-3: never out of same, margin or bind");
    Require(PickWantsGrind(true, Tier::C) && !PickWantsGrind(true, Tier::B) && !PickWantsGrind(false, Tier::C) &&
        !PickWantsGrind(false, Tier::A), "only a grind pick of tier C wants GRIND");

    std::uint32_t const fresh = 120000;
    Require(FeedCheck(true, false, false, true, false, 1000, 2000, fresh, false, false) == FeedSkip::Feed, "a fresh quest pick feeds");
    Require(FeedCheck(false, true, false, true, true, 1000, 2000, fresh, false, false) == FeedSkip::Feed, "a grind pick feeds grind");
    Require(FeedCheck(false, false, false, true, false, 1000, 2000, fresh, false, false) == FeedSkip::Purpose, "another purpose");
    Require(FeedCheck(true, false, true, true, false, 1000, 2000, fresh, false, false) == FeedSkip::Purpose, "a forced target");
    Require(FeedCheck(true, false, false, true, true, 1000, 2000, fresh, false, false) == FeedSkip::Purpose,
        "a grind pick never feeds a quest search");
    Require(FeedCheck(true, false, false, false, false, 0, 2000, fresh, false, false) == FeedSkip::None, "no pick");
    Require(FeedCheck(true, false, false, true, false, 1000, 1000 + fresh, fresh, false, false) == FeedSkip::Stale, "F5 (c) stale");
    Require(FeedCheck(true, false, false, true, false, 1000, 2000, fresh, true, false) == FeedSkip::Budget, "#416 resume pending");
    Require(FeedCheck(true, false, false, true, false, 1000, 2000, fresh, false, true) == FeedSkip::Budget, "#416 the pick ran out");
}

// Nachtrag 3: a failed plan attempt changes no choice counter - class B (this bot's / action's state) is restored, class P (the
// process-wide atomics) is buffered and only a chosen attempt adds it; never a restore or a fetch_sub on the shared counter.
struct FakeRejects
{
    std::uint32_t crossMap = 0;
    std::uint32_t levelGuard = 0;
};

void TestAttemptClasses()
{
    FakeRejects rejects;
    rejects.crossMap = 3;
    std::uint32_t skillLocked = 4;
    bool budgetExceeded = false;
    std::string resume = "quest:7";
    {
        ai::planner_write::AttemptRestore<FakeRejects, std::uint32_t, bool, std::string> classB(rejects, skillLocked, budgetExceeded,
            resume);
        rejects = FakeRejects();   // what the attempt's SetBestTarget does to them
        rejects.levelGuard = 9;
        skillLocked = 0;
        budgetExceeded = true;
        resume = "quest:2";
        classB.Restore();
    }
    Require(rejects.crossMap == 3 && rejects.levelGuard == 0 && skillLocked == 4 && !budgetExceeded && resume == "quest:7",
        "class B: a failed attempt changes no choice counter");

    std::atomic<std::uint32_t> shared{10};
    std::atomic<std::uint32_t> buffer{0};
    buffer.fetch_add(5);      // the attempt counts into its buffer
    shared.fetch_add(2);      // another bot's thread counts meanwhile
    Require(shared.load() == 12, "class P: a failed attempt (buffer dropped) leaves only the other thread's counts");
    ai::planner_write::CommitBuffered(shared, buffer);
    Require(shared.load() == 17, "class P: a chosen attempt adds its buffer once");
    std::atomic<std::uint32_t> none{0};
    ai::planner_write::CommitBuffered(shared, none);
    Require(shared.load() == 17, "class P: an empty buffer adds nothing");
}

// Section 5.4: 16 slots for a writing bot (the shadow keeps 8), N1 saturating deadCycles, N2 mem_evict, B19 first expired slot,
// the close counter.
void TestMemoryP3()
{
    Require(kMemorySlots == 16 && kShadowMemorySlots == 8, "16 slots for a writing bot, 8 for the shadow");
    MemorySlot w[kMemorySlots];
    MemorySlot s[kMemorySlots];
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        Close(w, kMemorySlots, 7, 1, 0, CloseReason::Dead, 1000 + i, 10, true);
        Close(s, kShadowMemorySlots, 7, 1, 0, CloseReason::Dead, 1000 + i, 10, false);
    }
    Require(DeadCycles(w, kMemorySlots, 7) == 255, "N1: a writing bot's deadCycles saturate at 255");
    Require(DeadCycles(s, kShadowMemorySlots, 7) == 1, "the shadow keeps P-2b's 255 -> 0 -> 1");
    Require(Closes(w, kMemorySlots, 7) == 256 && Closes(w, kMemorySlots, 8) == 0, "the close counter per key");

    MemorySlot m[4];
    bool evicted = true;
    Close(m, 4, 1, 1, 0, CloseReason::Suppressed, 1000, 500, false, &evicted);
    Require(!evicted, "an empty slot is no eviction");
    Close(m, 4, 2, 2, 0, CloseReason::Suppressed, 1000, 400);
    Close(m, 4, 3, 3, 0, CloseReason::Suppressed, 1000, 300);
    Close(m, 4, 4, 4, 0, CloseReason::Suppressed, 1000, 200);
    Close(m, 4, 9, 9, 0, CloseReason::Suppressed, 1450, 100000, false, &evicted);
    Require(m[1].key == 9 && m[2].key == 3 && m[3].key == 4 && !evicted, "B19: the first expired slot in slot order");
    Close(m, 4, 10, 10, 0, CloseReason::Suppressed, 1460, 100000, false, &evicted);
    Close(m, 4, 11, 11, 0, CloseReason::Suppressed, 1465, 100000, false, &evicted);
    Require(m[2].key == 10 && m[3].key == 11 && !evicted, "expired slots are taken first");
    Close(m, 4, 12, 12, 0, CloseReason::Suppressed, 1470, 100000, false, &evicted);
    Require(evicted && m[0].key == 12 && !IsClosed(m, 4, 1, 1470), "N2: every slot open: the oldest is evicted (mem_evict)");
    Close(m, 4, 12, 12, 0, CloseReason::Suppressed, 1480, 100000, false, &evicted);
    Require(!evicted && Closes(m, 4, 12) == 2, "the same key is no eviction");
}

// OB-10 pre-review 20.11. V-1 / V-2: the back_to_band pick (a forced grind pick at the band hub, set outside the hysteresis) is
// held by the next plan (Bind, or Same when the hub is the top candidate), never Switch: it reaches neither the mode switch nor
// the expire of the switch tick (the mode line stays QUEST). The next grind search inside MinBind is fed with it and a new write
// generation counts applied_grind; after MinBind the hold ends and the pick is replaced as usual.
void TestBackToBandV1V2()
{
    std::uint32_t const minBind = 120000;
    Hysteresis h;
    h.minBindMs = minBind;
    std::uint32_t const backAt = 50000;
    Pick bb;   // as NoteChoiceImpl sets it
    bb.has = true;
    bb.kind = Kind::Grind;
    bb.key = TargetKey(std::uint8_t(Kind::Grind), 0, 85u | 0x80000000u);
    bb.sinceMs = backAt;
    bb.atMs = backAt;
    bb.zone = 85;
    bb.forced = true;

    // V-1: the EvMode plan of the next tick and every plan inside MinBind.
    for (std::uint32_t dt : {1000u, 30000u, minBind - 1})
    {
        std::uint32_t const now = backAt + dt;
        Incumbent inc;
        inc.has = bb.has;
        inc.key = bb.key;
        inc.sinceMs = bb.sinceMs;
        inc.stillEligible = false;   // the band hub is not in the tier's pool
        Require(BackToBandHolds(true, inc.stillEligible, bb.has, bb.forced, bb.kind == Kind::Grind, bb.sinceMs, now, minBind),
            "V-1: the back_to_band pick holds inside MinBind");
        inc.stillEligible = true;
        Hold const other = HoldDecision(inc, 4242, 1.0e6f, false, now, h);
        Hold const same = HoldDecision(inc, bb.key, 1.0e6f, false, now, h);
        Require(other == Hold::Bind && same == Hold::Same, "V-1: a better candidate is held off by Bind, the hub itself is Same");
        Require(!ModeSwitchFrom(other) && !ModeSwitchFrom(same), "V-1: no mode switch from back_to_band (stays QUEST)");
        // The expire runs only in the switch branch (Switch / Forced); Bind and Same never get there.
        Require(other != Hold::Switch && other != Hold::Forced && same != Hold::Switch && same != Hold::Forced,
            "V-1: no expire from back_to_band");
    }
    {
        // Only a max_hold (forced: a complete quest's turn-in in tier A) replaces it inside MinBind; that switch is max_hold's and
        // its pick (a turn-in, not a tier C grind) wants QUEST.
        Incumbent inc;
        inc.has = true;
        inc.key = bb.key;
        inc.sinceMs = bb.sinceMs;
        inc.stillEligible = true;
        Require(HoldDecision(inc, 4242, 1.0f, true, backAt + 1000, h) == Hold::Forced, "a max_hold replaces it (Forced)");
        Require(!PickWantsGrind(false, Tier::A), "the max_hold turn-in wants QUEST");
    }
    Require(!BackToBandHolds(false, false, true, true, true, backAt, backAt + 1000, minBind), "V-1: only a writing bot holds it");
    Require(!BackToBandHolds(true, false, true, false, true, backAt, backAt + 1000, minBind), "a normal grind pick is not held");

    // V-2: the next grind search inside MinBind is fed (fed), a new write generation is applied_grind.
    std::uint32_t const next = backAt + 20000;
    Require(FeedCheck(false, true, false, bb.has, true, bb.atMs, next, minBind, false, false) == FeedSkip::Feed,
        "V-2: the next grind search is fed with the back_to_band pick");
    Require(ApplyOutcomeOf(7, 8, false) == ApplyOutcome::Applied, "V-2: a new write generation is applied (applied_grind)");
    Require(ApplyOutcomeOf(7, 7, true) == ApplyOutcome::Parked && ApplyOutcomeOf(7, 7, false) == ApplyOutcome::Denied,
        "no new generation: parked (#485) or denied (arbiter)");
    // V-2: after MinBind the hold ends: an incumbent outside the pool is replaced (Switch), the pick is no longer fresh.
    std::uint32_t const late = backAt + minBind;
    Require(!BackToBandHolds(true, false, bb.has, bb.forced, true, bb.sinceMs, late, minBind), "V-2: the hold ends at MinBind");
    Incumbent gone;
    gone.has = true;
    gone.key = bb.key;
    gone.sinceMs = bb.sinceMs;
    gone.stillEligible = false;
    Require(HoldDecision(gone, 4242, 1.0f, false, late, h) == Hold::Switch, "V-2: after MinBind the pick is replaced as usual");
    Require(FeedCheck(false, true, false, bb.has, true, bb.atMs, late, minBind, false, false) == FeedSkip::Stale,
        "V-2: an unconfirmed back_to_band pick is stale after MinBind (no feed)");
}

// OB-10 V-3 (deviation 6 rejected): rule 5 locks only after reject, budget, parked, denied and zone_empty; none, stale and purpose
// never set it, so an open pending expire ends despite repeated purpose and stale fallbacks.
void TestRule5V3()
{
    Require(FallbackLocks(FeedFallback::Reject) && FallbackLocks(FeedFallback::Budget) && FallbackLocks(FeedFallback::Parked) &&
        FallbackLocks(FeedFallback::Denied) && FallbackLocks(FeedFallback::ZoneEmpty), "V-3: the five lock reasons");
    Require(!FallbackLocks(FeedFallback::None) && !FallbackLocks(FeedFallback::Stale) && !FallbackLocks(FeedFallback::Purpose),
        "V-3: none, stale and purpose never lock");

    std::uint32_t const minBind = 120000;
    std::uint32_t fallbackAt = 0;
    ExpireFacts f = QuestFacts(TravelPhase::Work);   // the pending expire waits for WORK -> TRAVEL (F8 (a))
    std::uint32_t now = 1000;
    Require(Decide2to5(f, false) == ExpireStep::DeniedWork && PendingStarts(ExpireStep::DeniedWork), "the expire is pending");
    for (int i = 0; i < 50; ++i)
    {
        now += 5000;   // a grind search meets the quest pick (purpose), the pick goes stale between plan ticks, no pick
        NoteFallback(fallbackAt, FeedFallback::Purpose, now);
        NoteFallback(fallbackAt, FeedFallback::Stale, now);
        NoteFallback(fallbackAt, FeedFallback::None, now);
    }
    Require(fallbackAt == 0, "V-3: purpose / stale / none leave the rule 5 time unset");
    f.phase = TravelPhase::Travel;
    f.fallbackBlock = FallbackBlocks(fallbackAt, now, minBind);
    Require(!f.fallbackBlock && Decide2to5(f, false) == ExpireStep::Go, "V-3: the pending expire ends despite those fallbacks");

    // A failed attempt with the current pick locks for MinBind, then the expire goes on.
    NoteFallback(fallbackAt, FeedFallback::Reject, now);
    Require(fallbackAt == now, "V-3: reject sets the time");
    f.fallbackBlock = FallbackBlocks(fallbackAt, now + 1000, minBind);
    Require(Decide2to5(f, false) == ExpireStep::BlockedFb, "V-3: reject blocks for MinBind");
    f.fallbackBlock = FallbackBlocks(fallbackAt, now + minBind, minBind);
    Require(Decide2to5(f, false) == ExpireStep::Go, "V-3: after MinBind the expire goes on");
    // The starvation case of the review: one reject at t0, then purpose / stale every few seconds; the lock is not renewed and the
    // pending expire goes at t0 + MinBind.
    std::uint32_t const t0 = 500000;
    std::uint32_t lockAt = 0;
    NoteFallback(lockAt, FeedFallback::Reject, t0);
    for (std::uint32_t t = t0 + 3000; t < t0 + minBind; t += 3000)
    {
        NoteFallback(lockAt, (t / 3000) % 2 ? FeedFallback::Purpose : FeedFallback::Stale, t);
        f.fallbackBlock = FallbackBlocks(lockAt, t, minBind);
        Require(Decide2to5(f, false) == ExpireStep::BlockedFb, "V-3: blocked inside MinBind of the reject");
    }
    Require(lockAt == t0, "V-3: purpose / stale do not renew the lock");
    f.fallbackBlock = FallbackBlocks(lockAt, t0 + minBind, minBind);
    Require(Decide2to5(f, false) == ExpireStep::Go, "V-3: the pending expire goes at t0 + MinBind");
    std::uint32_t zero = 0;
    NoteFallback(zero, FeedFallback::Denied, 0);
    Require(zero == 1, "a lock at time 0 is stored as 1 (0 = no fallback)");
}

// Section 6.1 / 7.5: [PlanMin] at Planner 0 / 1 is the P-2b line byte for byte; Planner = 2 appends the P-3a fields; the longest
// line fits kPlanMinBuf.
void TestPlanMinP3()
{
    std::string const golden = "[PlanMin] cohort=A min=7 plans=0 keeps=0 switches=0 tierA=0 tierB=0 tierC=0 dead_wait=0 dead_close=0 "
        "zone_change=0 zone_change_dead=0 cut=0 skip=0 timer=0 over_budget=0 overwritten=0 overwritten_group=0 agree=0 disagree=0 "
        "r1_mismatch=0 max_hold=0 repeat_cap=0 back_to_band=0 floor_below=0 floor_unknown=0 relocate=0 reloc_unknown=0 "
        "plan_dropped=0 ms_p50=0 ms_max=0 back_to_band_would=0 back_to_band_repeat=0 expire_work_override_would=0 "
        "overwritten_shadow=0 na_purpose=0 na_stale=0 overwritten_neutral=0 switch_ms=0 passive_ticks=0 agree_grind=0 "
        "disagree_grind=0 back_to_band_would_quest=0 back_to_band_would_grind=0";
    std::string const tail = " expired=0 expire_denied=0 expire_denied_cooldown=0 expire_denied_work=0 expire_denied_lane=0 "
        "expire_denied_group=0 expire_denied_thread=0 expire_denied_zone_unknown=0 expire_work_override=0 expire_repeat=0 "
        "expire_blocked_fb=0 expire_pending=0 expire_after_work=0 fed=0 applied_quest=0 applied_grind=0 applied_live_quest=0 "
        "applied_live_grind=0 fallback_reject=0 fallback_stale=0 fallback_none=0 fallback_purpose=0 fallback_budget=0 "
        "fallback_denied=0 fallback_parked=0 fallback_zone_empty=0 fb_arbiter=0 fb_cohort=0 turnin_unreached=0 mem_evict=0 "
        "mode_quest_grind=0 mode_grind_quest=0 mode_denied=0";
    PlanMinCounts z;
    char buf[kPlanMinBuf];
    std::size_t n = FormatPlanMin(buf, sizeof(buf), 'A', 7, z);
    Require(std::string(buf) == golden && n == golden.size(), "Planner 0 / 1: the [PlanMin] golden line, byte-identical");
    FormatPlanMin(buf, sizeof(buf), 'A', 7, z, false);
    Require(std::string(buf) == golden, "p3 false is the default");
    n = FormatPlanMin(buf, sizeof(buf), 'A', 7, z, true);
    Require(std::string(buf) == golden + tail && n == golden.size() + tail.size(), "Planner = 2: the P-3a fields appended");

    PlanMinCounts a;
    a.expired = 1;
    a.expireDenied = 2;
    a.fed = 3;
    a.fallbackZoneEmpty = 4;
    a.memEvict = 5;
    a.modeDenied = 6;
    PlanMinCounts const b = a;
    Add(a, b);
    FormatPlanMin(buf, sizeof(buf), 'B', 8, a, true);
    std::string const line(buf);
    std::string const last = " mode_denied=12";
    Require(line.find(" expired=2 expire_denied=4 ") != std::string::npos && line.find(" fed=6 ") != std::string::npos &&
        line.find(" fallback_zone_empty=8 ") != std::string::npos && line.find(" mem_evict=10 ") != std::string::npos &&
        line.size() > last.size() && line.compare(line.size() - last.size(), last.size(), last) == 0,
        "Add sums the P-3a fields into their places");

    PlanMinCounts full;
    static_assert(sizeof(PlanMinCounts) % sizeof(std::uint32_t) == 0, "PlanMinCounts holds only uint32 fields");
    std::uint32_t fields[sizeof(PlanMinCounts) / sizeof(std::uint32_t)];
    for (std::uint32_t& v : fields)
        v = 4294967295u;
    std::memcpy(&full, fields, sizeof(full));
    char big[kPlanMinBuf];
    n = FormatPlanMin(big, sizeof(big), 'B', 4294967295u, full, true);
    std::string const maxTail = " mode_denied=4294967295";
    Require(n == std::strlen(big) && n + 1 < sizeof(big) && n > maxTail.size() &&
        std::string(big).compare(n - maxTail.size(), maxTail.size(), maxTail) == 0,
        "the longest [PlanMin] line fits kPlanMinBuf, not cut");
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
    TestNotesAndComplete();
    TestWriteCohort();
    TestExpireRules();
    TestPendingExpire();
    TestModeAndFeed();
    TestAttemptClasses();
    TestMemoryP3();
    TestPlanMinP3();
    TestBackToBandV1V2();
    TestRule5V3();
    std::cout << "planner_shadow_policy_tests: OK\n";
    return 0;
}
