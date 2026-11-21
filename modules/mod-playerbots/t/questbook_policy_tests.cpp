// A3 step 2 quest book core (QuestBookPolicy.h, AiPlayerbot.QuestBook). Hand-rolled Require(), std::exit(1) on failure, no
// gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "QuestBookPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

ai::questbook::Facts Quest(int bot, int quest)
{
    ai::questbook::Facts f;
    f.questId = 100;
    f.botLevel = bot;
    f.questLevel = quest;
    f.grayLevel = bot > 10 ? bot - 8 : 0;
    f.freeSlots = 10;
    f.inLog = true;
    return f;
}
}

int main()
{
    using namespace ai::questbook;
    Params const p;   // aboveLevels 1, zoneAboveLevels 5, logFullSlots 4

    // 1) The switch: cohort modes 0..3, anything else is off.
    {
        Require(ValidMode(0) && ValidMode(3) && !ValidMode(4), "modes 0..3");
        Require(SanitizeMode(7) == 0 && SanitizeMode(2) == 2, "unknown mode -> 0");
        Require(!Applies(0, 5) && Applies(1, 4) && Applies(1, 5), "0 off, 1 all");
        Require(Applies(2, 5) && !Applies(2, 4), "2 = cohort B (odd)");
        Require(Applies(3, 4) && !Applies(3, 5), "3 = cohort A (even)");
        Require(!Applies(9, 5), "invalid mode never applies");
    }

    // 2) One level window.
    {
        Require(InWindow(20, 21, 0, 12, false, p), "+1 is inside");
        Require(WindowReason(20, 22, 0, 12, false, p) == NoReason::AboveWindow, "+2 is above the window");
        Require(WindowReason(20, 12, 0, 12, false, p) == NoReason::Grey, "the grey level itself is grey");
        Require(InWindow(20, 13, 0, 12, false, p), "one above grey is inside");
        Require(WindowReason(20, 20, 22, 12, false, p) == NoReason::BelowMinLevel, "MinLevel not reached");
        Require(InWindow(20, 30, 25, 12, true, p), "a class quest is always inside");
        Require(InWindow(5, 1, 0, 0, false, p), "grayLevel 0: nothing is grey");
        Params wide;
        wide.aboveLevels = 4;
        Require(InWindow(20, 24, 0, 12, false, wide) && !InWindow(20, 25, 0, 12, false, wide), "the window follows the parameter");
    }

    // 3) Zone fits.
    {
        Require(ZoneFits(20, 0, p) && ZoneFits(20, -1, p), "unknown area fits");
        Require(ZoneFits(20, 25, p) && !ZoneFits(20, 26, p), "+5 fits, +6 not");
    }

    // 4) CanAccept.
    {
        Facts f = Quest(20, 21);
        f.inLog = false;
        Require(CanAccept(f, p).IsYes(), "in window, free log");
        f.excluded = true;
        Require(CanAccept(f, p).reason == NoReason::Excluded, "QB5 list first");
        f.excluded = false;
        f.questLevel = 25;
        Require(CanAccept(f, p).reason == NoReason::AboveWindow, "+5 is no longer accepted (W3)");
        f.questLevel = 20;
        f.freeSlots = 4;
        Require(CanAccept(f, p).reason == NoReason::LogFull, "4 free slots = full");
        f.classQuest = true;
        Require(CanAccept(f, p).IsYes(), "a class quest ignores the log-full rule");
        f.excluded = true;
        Require(CanAccept(f, p).IsNo(), "but not the QB5 list");
    }

    // 5) CanTarget.
    {
        Facts f = Quest(20, 21);
        Require(CanTarget(f, Purpose::Objective, p).IsYes(), "objective inside");
        Require(CanTarget(f, Purpose::Giver, p).verdict == Verdict::CheckAtRuntime, "giver unknown -> check live");
        Require(CanTarget(f, Purpose::Giver, p).reason == NoReason::GiverAbsent, "check reason is the giver");
        f.giverSpawned = Tri::No;
        Require(CanTarget(f, Purpose::Giver, p).reason == NoReason::GiverAbsent && CanTarget(f, Purpose::Giver, p).IsNo(), "no giver");
        f.giverSpawned = Tri::Yes;
        Require(CanTarget(f, Purpose::Giver, p).IsYes(), "giver spawned");
        f.areaLevel = 27;
        Require(CanTarget(f, Purpose::Objective, p).reason == NoReason::ZoneTooHigh, "zone too high");
        f.areaLevel = 0;
        f.parked = true;
        Require(CanTarget(f, Purpose::Objective, p).reason == NoReason::Parked, "parked is no target");
        f.parked = false;
        f.closed = true;
        Require(CanTarget(f, Purpose::Objective, p).reason == NoReason::Closed, "closed is no target");
        f.closed = false;
        f.questLevel = 22;
        Require(CanTarget(f, Purpose::Objective, p).reason == NoReason::AboveWindow, "same window as accept");
    }

    // 6) CanTurnIn: no level window, no zone rule, the QB5 list does not stop a turn-in.
    {
        Facts f = Quest(20, 30);
        f.complete = true;
        f.canReward = true;
        f.takerSpawned = Tri::Yes;
        f.areaLevel = 40;
        f.excluded = true;
        Require(CanTurnIn(f).IsYes(), "complete quest far above the window may be handed in");
        f.takerSpawned = Tri::Unknown;
        Require(CanTurnIn(f).verdict == Verdict::CheckAtRuntime, "taker unknown -> check live");
        f.takerSpawned = Tri::No;
        Require(CanTurnIn(f).reason == NoReason::TakerAbsent, "no taker");
        f.takerSpawned = Tri::Yes;
        f.hasMoney = false;
        Require(CanTurnIn(f).reason == NoReason::NoMoney, "money first");
        f.hasMoney = true;
        f.canReward = false;
        Require(CanTurnIn(f).reason == NoReason::CannotReward, "cannot reward (bags)");
        f.complete = false;
        Require(CanTurnIn(f).reason == NoReason::NotComplete, "not complete");
        f.inLog = false;
        Require(CanTurnIn(f).reason == NoReason::NotInLog, "not in log");
    }

    // 7) Time and progress.
    {
        Require(Before(10, 20) && !Before(20, 10) && !Before(5, 5), "before");
        Require(Before(0xFFFFFF00u, 0x10u), "before across the wrap");
        Require(Progressed(3, 4) && !Progressed(4, 4), "progress is a different seq");
        Require(Progressed(0xFFFFFFFFu, 0), "a wrapped counter still counts as progress (!=, not >)");
        Require(HasProgress(1, 0) && HasProgress(0, 2) && !HasProgress(0, 0), "progress = seq or counts, never text");

        CreditBook b;
        Require(b.Seq(7) == 0 && !b.Bump(0), "empty book; quest 0 is no quest");
        Require(b.Bump(7) && b.Bump(7) && b.Seq(7) == 2, "two credits");
        Require(b.Reset(7) && b.Seq(7) == 0, "accept resets to 0");
        Require(b.Bump(8) && b.Track(8) && b.Seq(8) == 1, "a credit before the first snapshot is kept (OB-10 #910 note 2)");
        Require(b.Track(9) && b.Seq(9) == 0 && !b.Track(0), "a new quest is tracked from 0; quest 0 is no quest");
        b.Forget(8);
        b.Forget(9);
        b.Forget(7);
        Require(b.Used() == 0, "forget frees the slot");
        for (std::uint32_t q = 1; q <= CreditBook::Slots; ++q)
            Require(b.Bump(q), "fill the book");
        Require(!b.Bump(999), "a full book drops the bump");
        Require(b.Bump(5) && b.Seq(5) == 2, "known quests still count");
    }

    // 8) Park slots.
    {
        ParkBook pk;
        pk.Park(1, 0, 1, 1000);
        pk.Park(2, 5000, 1, 1000);
        Require(pk.IsParked(1, 999999) && pk.IsParked(2, 4999) && !pk.IsParked(2, 5000), "open-ended and timed park");
        std::uint32_t ids[ParkBook::Slots];
        Require(pk.Parked(1000, ids, ParkBook::Slots) == 2, "two parked");
        pk.Unpark(1);
        Require(!pk.IsParked(1, 1000), "unpark");
        ParkBook full;
        for (std::uint32_t q = 10; q < 10 + ParkBook::Slots; ++q)
            full.Park(q, q == 10 ? 0 : 2000 + q, 1, 1000);   // 10 open-ended, the others until 2000 + q
        Require(full.Parked(1000, ids, ParkBook::Slots) == ParkBook::Slots, "full");
        full.Park(99, 9000, 1, 1000);
        Require(full.IsParked(99, 1000) && !full.IsParked(11, 1000) && full.IsParked(12, 1000) && full.IsParked(10, 1000),
            "a full book replaces the entry that ends first, never the open-ended one");
        full.Park(12, 0, 1, 1000);
        Require(full.IsParked(12, 999999), "parking a parked quest again updates it in place");
        full.Park(0, 0, 1, 1000);
        Require(full.Parked(1000, ids, ParkBook::Slots) == ParkBook::Slots, "quest 0 is ignored");
    }

    // 9) Close memory.
    {
        CloseBook c;
        c.Close(5, 1, 8000, 2, 1000);
        c.Close(5, 2, 500, 2, 1000);   // already over: ignored
        Require(c.IsClosed(5, 1, 7999) && !c.IsClosed(5, 1, 8000), "closed until untilMs");
        Require(!c.IsClosed(5, 2, 1000), "a close in the past is not stored");
        std::uint32_t const sources[3] = {1, 2, 3};
        Require(c.OpenSources(5, sources, 3, 2000) == 2, "two of three sources open");
        c.Close(5, 2, 8000, 2, 1000);
        c.Close(5, 3, 8000, 2, 1000);
        Require(c.OpenSources(5, sources, 3, 2000) == 0, "all closed -> park");
        Require(c.OpenSources(5, sources, 3, 9000) == 3, "all reopen after untilMs");
        for (std::uint32_t q = 100; q < 100 + CloseBook::Slots; ++q)
            c.Close(q, 1, 3000 + q, 1, 1000);
        c.Close(77, 1, 50000, 1, 1000);
        Require(c.IsClosed(77, 1, 1000), "a full memory still takes a new close");
    }

    // 9b) QB6/QB7 pair: the close memory follows the planner's rule 1:1 (PlannerShadowPolicy.h Close; CLI-Projekt 20.11. Q3 (a)).
    {
        std::uint8_t const dead = std::uint8_t(CloseReason::Dead), r9 = std::uint8_t(CloseReason::R9);
        Require(dead == 1 && r9 == 2 && std::uint8_t(CloseReason::Suppressed) == 3 && std::uint8_t(CloseReason::None) == 0,
            "reason values as planner::CloseReason");
        CloseBook c;
        c.Close(5, 11, 9000, std::uint8_t(CloseReason::None), 1000);
        Require(!c.IsClosed(5, 11, 1000) && c.Closes(5, 11) == 0, "reason None is no close");
        c.Close(5, 11, 1000, r9, 1000);
        Require(!c.IsClosed(5, 11, 1000), "untilMs == nowMs (closeMs 0) is no close");
        c.Close(0, 11, 9000, r9, 1000);
        Require(!c.IsClosed(0, 11, 1000), "quest 0 is no close");
        c.Close(5, 11, 1000 + 0x80000000u, r9, 1000);
        Require(!c.IsClosed(5, 11, 1000), "a span of 2^31 ms or more is no close (fail-closed, never open-ended)");

        // deadCycles: +1 only on Dead, kept on the same key across other reasons and after the close expired.
        c.Close(5, 11, 3000, dead, 1000);
        Require(c.IsClosed(5, 11, 2999) && c.DeadCycles(5, 11) == 1 && c.Closes(5, 11) == 1, "first dead close");
        c.Close(5, 11, 9000, r9, 2000);
        Require(c.DeadCycles(5, 11) == 1 && c.Closes(5, 11) == 2, "another reason keeps deadCycles, closes counts");
        c.Close(5, 11, 20000, dead, 10000);   // the previous close expired at 9000
        Require(c.DeadCycles(5, 11) == 2 && c.Closes(5, 11) == 3, "the counters survive an expired close of the same key");
        Require(c.DeadCycles(5, 12) == 0 && c.Closes(6, 11) == 0, "other keys / quests have no counters");
        Require(!c.DropRequested(5, 10000), "no drop asked");
        c.Close(5, 12, 20000, r9, 10000, true);
        Require(c.DropRequested(5, 19999) && !c.DropRequested(5, 20000) && !c.DropRequested(6, 10000), "drop asked while live");

        // A full memory: the same key first, else the first expired slot in field order, else the slot closed longest ago.
        CloseBook f;
        for (std::uint32_t i = 0; i < CloseBook::Slots; ++i)
            f.Close(100 + i, 1, i == 0 ? 60000 : 40000 + i * 100, r9, 1000 + i * 10);   // 100: closed first, ends last; 101 ends first
        f.Close(200, 1, 60000, r9, 2000);
        Require(f.IsClosed(200, 1, 2000) && !f.IsClosed(100, 1, 2000) && f.IsClosed(101, 1, 2000),
            "no expired slot: the oldest closedAtMs goes (not the one that ends first)");
        CloseBook g;
        for (std::uint32_t i = 0; i < CloseBook::Slots; ++i)
            g.Close(100 + i, 1, (i == 3 || i == 7) ? 1500 : 50000, r9, 1000);
        g.Close(300, 1, 60000, r9, 2000);   // slots 3 and 7 expired at 1500: slot 3 (first in field order) is taken
        Require(g.IsClosed(300, 1, 2000) && g.Closes(103, 1) == 0 && g.Closes(107, 1) == 1,
            "the first expired slot in field order is taken");
        g.Close(110, 1, 70000, r9, 3000);
        Require(g.Closes(110, 1) == 2 && g.IsClosed(110, 1, 69999), "the same key is updated in place");
        CloseBook s;
        for (std::uint32_t i = 0; i < 300; ++i)
            s.Close(9, 1, 5000 + i, dead, 1000 + i);
        Require(s.DeadCycles(9, 1) == 255 && s.Closes(9, 1) == 255, "counters saturate at 255");
    }

    // 9c) QB6/QB7 episode: one, with a hard end (fail-closed).
    {
        EpisodeBook e;
        Require(!e.Active(1000), "none at start");
        Require(!e.Start(0, EpisodeKind::Escort, 60000, 1000), "quest 0 refused");
        Require(!e.Start(80395, EpisodeKind::None, 60000, 1000), "kind None refused");
        Require(!e.Start(80395, EpisodeKind::GoSummon, 0, 1000), "duration 0 refused (no lock without an end)");
        Require(e.Start(80395, EpisodeKind::GoSummon, 90000, 1000), "QB6 started");
        Episode const* a = e.Active(1000);
        Require(a && a->questId == 80395 && a->kind == EpisodeKind::GoSummon && a->stage == 0 && a->untilMs == 91000, "active");
        Require(!e.Start(4261, EpisodeKind::Escort, 60000, 2000), "a second live episode is refused");
        Require(!e.Advance(4261, 1, 2000) && e.Advance(80395, 1, 2000) && e.Active(2000)->stage == 1,
            "advance only the live episode of the same quest");
        Require(e.Active(90999) && e.Active(90999)->untilMs == 91000, "advance never moves the end");
        Require(!e.Active(91000) && e.LastEnd() == EpisodeEnd::Expired, "the hard end; found expired = cleared, reason Expired");
        Require(!e.Advance(80395, 2, 91000) && !e.End(80395, EpisodeEnd::QuestComplete), "nothing left after the end");

        Require(e.Start(4261, EpisodeKind::Escort, 10000000, 100000), "QB7 started with a too long duration");
        Require(e.Active(100000) && e.Active(100000)->untilMs == 100000 + MaxEpisodeMs, "cut to MaxEpisodeMs");
        Require(e.Active(100000 + MaxEpisodeMs - 1) && !e.Active(100000 + MaxEpisodeMs), "the cap is the end");

        Require(e.Start(4261, EpisodeKind::Escort, 60000, 200000), "restart after the end");
        Require(!e.End(5944, EpisodeEnd::NpcDead) && e.Active(200001), "End of another quest does nothing");
        Require(e.End(4261, EpisodeEnd::NpcDead) && !e.Active(200001) && e.LastEnd() == EpisodeEnd::NpcDead, "End with reason");

        EpisodeBook w;   // across the getMSTime wrap
        Require(w.Start(7, EpisodeKind::Escort, 60000, 0xFFFFF000u), "start just before the wrap");
        Require(w.Active(0xFFFFF000u + 59999u) && !w.Active(0xFFFFF000u + 60000u), "exact end across the wrap");
        EpisodeBook z;   // an episode nobody looked at for a long time stays over (no wrap back to active within ~49 days)
        Require(z.Start(8, EpisodeKind::GoSummon, 60000, 1000), "start");
        Require(!z.Active(61000u + 0x80000000u), "2^31 ms after the end (where a Before() check would wrap back to active): not active");
    }

    // 10) QB8 diff.
    {
        std::uint32_t const prev[4] = {1, 2, 3, 0};
        std::uint32_t const now[2] = {3, 1};
        std::uint32_t out[4];
        Require(LeftLog(prev, 4, now, 2, out, 4) == 1 && out[0] == 2, "quest 2 left the log");
        Require(LeftLog(prev, 4, nullptr, 0, out, 2) == 2, "max caps the output");
        Require(std::strcmp(LeftReasonName(LeftReason::TurnedIn), "turned_in") == 0, "reason names");
        Require(std::strcmp(NoReasonName(NoReason::AboveWindow), "above_window") == 0, "no-reason names");
    }

    // 10b) twow-repo#724: the turn-in label (a repeatable quest is never "rewarded"; a reward note is the proof).
    {
        Require(ClassifyLeft(true, false, false) == LeftReason::TurnedIn, "rewarded status -> turned_in");
        Require(ClassifyLeft(false, true, false) == LeftReason::TurnedIn, "reward note (repeatable) -> turned_in");
        Require(ClassifyLeft(false, true, true) == LeftReason::TurnedIn, "a reward note wins over failed");
        Require(ClassifyLeft(false, false, true) == LeftReason::Failed, "failed");
        Require(ClassifyLeft(false, false, false) == LeftReason::Dropped, "no reward, no note -> dropped");

        std::uint32_t ids[3] = {};
        std::size_t n = 0;
        n = NoteQuestId(ids, n, 3, 889);
        n = NoteQuestId(ids, n, 3, 889);
        Require(n == 1 && HasQuestId(ids, n, 889), "a repeat is kept once");
        n = NoteQuestId(ids, n, 3, 0);
        Require(n == 1, "quest 0 is ignored");
        n = NoteQuestId(ids, n, 3, 5042);
        n = NoteQuestId(ids, n, 3, 5043);
        n = NoteQuestId(ids, n, 3, 5044);
        Require(n == 3 && !HasQuestId(ids, n, 889) && HasQuestId(ids, n, 5042) && HasQuestId(ids, n, 5044), "full: oldest dropped");
        Require(!HasQuestId(ids, 0, 5042) && !HasQuestId(nullptr, 3, 5042), "empty / null list");
        Require(NoteQuestId(nullptr, 2, 3, 5045) == 2 && NoteQuestId(ids, 1, 0, 5045) == 1, "null list / cap 0 unchanged");
    }

    // 11) QB4 KPI: complete but not turned in.
    {
        Require(!ShouldReportStale(0, 5000000, 1800000, false), "not complete");
        Require(!ShouldReportStale(1000, 1000 + 1799999, 1800000, false), "just below 30 min");
        Require(ShouldReportStale(1000, 1000 + 1800000, 1800000, false), "30 min");
        Require(!ShouldReportStale(1000, 1000 + 1800000, 1800000, true), "only once");
        Require(ShouldReportStale(0xFFFF0000u, 0xFFFF0000u + 1800000u, 1800000, false), "across the wrap");
    }

    std::cout << "questbook_policy_tests: all passed\n";
    return 0;
}
