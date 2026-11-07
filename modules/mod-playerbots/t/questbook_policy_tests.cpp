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
