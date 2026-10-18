#include "TransportBoardPolicy.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// twow-repo#342 (S3b-0): TransportWaitTimeoutMin clamp and threshold, the stop helpers, the per-wait probe record, the
// [TransportDeck] / [TransportBoard] formats and the probe's own line budget.
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

using namespace ai::transport_board;
}

int main()
{
    // D3: the key, clamped; the threshold the callers pass; byte-identical to S1 at the default.
    Require(ClampWaitTimeoutMin(15) == 15u, "15 stays");
    Require(ClampWaitTimeoutMin(0) == 15u, "0 -> default 15");
    Require(ClampWaitTimeoutMin(-3) == 15u, "negative -> default 15");
    Require(ClampWaitTimeoutMin(1) == 1u, "1 minute allowed");
    Require(ClampWaitTimeoutMin(240) == 240u, "240 allowed");
    Require(ClampWaitTimeoutMin(241) == 240u, "above 240 -> 240");
    Require(ClampWaitTimeoutMin(2147483647) == 240u, "int max -> 240");
    Require(WaitTimeoutMsOf(15) == 900000u, "15 min = 900000 ms");
    Require(WaitTimeoutMsOf(15) == ai::transport_edge::WaitTimeoutMs, "default = the S1 constant");
    Require(WaitTimeoutMsOf(0) == 900000u, "0 -> the default threshold");
    Require(WaitTimeoutMsOf(1) == 60000u, "1 min");
    Require(WaitTimeoutMsOf(240) == 14400000u, "240 min");
    Require(WaitTimeoutMsOf(100000) == 14400000u, "unclamped input capped at 240 min (no overflow)");

    // Stop frame (Transport.cpp:162): inclusive at arrive, exclusive at depart, never when arrive == depart.
    Require(AtStopFrame(1000, 1000, 2000), "at arrive");
    Require(AtStopFrame(1999, 1000, 2000), "just before depart");
    Require(!AtStopFrame(2000, 1000, 2000), "at depart");
    Require(!AtStopFrame(999, 1000, 2000), "before arrive");
    Require(!AtStopFrame(1000, 1000, 1000), "no stop without a delay");
    Require(StopNear(15.94f) && StopNear(20.0f) && StopNear(0.0f), "dock to its own stop within 20 yd");
    Require(!StopNear(20.01f) && !StopNear(46.0f) && !StopNear(-1.0f), "another stop / invalid distance");

    // Per-wait record: one probe per dock wait.
    {
        DeckWait d;
        Require(DeckProbeOpen(d, 177233, 1, -4197.31f, 3286.63f, 1000), "first wait tick: probe open");
        Require(DeckProbeOpen(d, 177233, 1, -4197.31f, 3286.63f, 5000), "not probed yet: still open");
        d.probed = true;
        Require(!DeckProbeOpen(d, 177233, 1, -4197.31f, 3286.63f, 9000), "probed: closed for this wait");
        Require(d.wait.startMs == 1000u && d.wait.lastMs == 9000u, "same wait, last seen kept");
        // dock / stop alternation within 20 yd (13.85 yd) is the same wait.
        Require(!DeckProbeOpen(d, 177233, 1, -4197.31f + 13.85f, 3286.63f, 12000), "dock / stop node: same wait");
        // a gap above GapMs, another ride, another dock or another map is a new wait.
        Require(DeckProbeOpen(d, 177233, 1, -4197.31f, 3286.63f, 12000 + ai::transport_edge::GapMs + 1), "gap: new wait");
        Require(d.wait.startMs == 12000u + ai::transport_edge::GapMs + 1u, "new wait starts now");
        d.probed = true;
        Require(DeckProbeOpen(d, 176244, 1, -4197.31f, 3286.63f, 400000), "another ride: new wait");
        d.probed = true;
        Require(DeckProbeOpen(d, 176244, 1, -4197.31f + 25.0f, 3286.63f, 401000), "25 yd away: new wait");
        d.probed = true;
        Require(DeckProbeOpen(d, 176244, 0, -4197.31f + 25.0f, 3286.63f, 402000), "another map: new wait");
        // within the gap, wrap-safe.
        DeckWait w;
        Require(DeckProbeOpen(w, 0, 0, 1.0f, 1.0f, 0xFFFFFF00u), "entry 0 dock: open");
        w.probed = true;
        Require(!DeckProbeOpen(w, 0, 0, 1.0f, 1.0f, 0x00000100u), "clock wrap within the gap: same wait");
    }

    // Probe points: fixed, bounded.
    Require(DeckProbeTries == 8u, "8 fixed points");
    Require(DeckProbeOffsets[0][0] == 0.0f && DeckProbeOffsets[0][1] == 0.0f, "first point = the floor");
    Require(DeckProbeOffsets[7][0] == 6.0f && DeckProbeOffsets[7][1] == -6.0f, "last point");
    Require(DeckProbeUp == 15.0f && DeckProbeDown == 30.0f, "long ray 15 up / 30 down");

    // [TransportDeck] line, exact.
    {
        DeckProbe p;
        p.tries = 8;
        p.staticShort = 0;
        p.deckShort = 6;
        p.deckLong = 8;
        p.dynLong = 7;
        p.model = true;
        p.inTree = true;
        p.longDzSet = true;
        p.longDz = -0.25f;
        Require(FormatDeck(42, 176231, 3015, 6.0f, p) ==
                "[TransportDeck] bot=42 ride=176231 display=3015 floor_off=6.00 tries=8 static=0 deck_short=6 deck_long=8 "
                "dyn_long=7 mdl=1 in_tree=1 long_dz=-0.25",
            "deck line");
        DeckProbe none;
        Require(FormatDeck(7, 164871, 3031, -17.0f, none) ==
                "[TransportDeck] bot=7 ride=164871 display=3031 floor_off=-17.00 tries=0 static=0 deck_short=0 deck_long=0 "
                "dyn_long=0 mdl=0 in_tree=0 long_dz=-999.00",
            "deck line without a long hit");
        Snapshot s;
        s.probes = 12;
        s.dropped = 3;
        s.minute = 29000000;
        s.minutes = 2;
        Require(FormatSummary(s) == "[TransportBoard] summary probes=12 dropped=3 min=29000000 mins=2", "summary line");
        // S3b-1: the S3b-0 line unchanged with the fix off, the recovery counters after dropped= with it on.
        s.recoverNear = 4;
        s.recoverSearch = 2;
        s.recoverNoRoute = 1;
        Require(FormatSummary(s, false) == "[TransportBoard] summary probes=12 dropped=3 min=29000000 mins=2",
            "fix off: the S3b-0 summary, byte-identical");
        Require(FormatSummary(s, true) ==
                "[TransportBoard] summary probes=12 dropped=3 stranded_near=4 stranded_search=2 stranded_noroute=1 min=29000000 "
                "mins=2",
            "fix on: summary line with recoveries");
    }

    // Own budget: 30 lines a minute, counted after the roll and before the cap, dropped counted, minute roll by CAS.
    Require(LinesPerMinute == 30u, "own budget 30 lines/min");
    {
        Counters c;
        Snapshot sn;
        Require(!c.Roll(600, sn), "no summary for the very first minute");
        for (std::uint32_t i = 0; i < LinesPerMinute; ++i)
            Require(c.Take(), "within the cap");
        Require(!c.Take() && !c.Take(), "over the cap");
        c.probes.fetch_add(32);
        Require(!c.Roll(659, sn), "same minute: no summary");
        Require(c.Roll(660, sn), "next minute: summary");
        Require(sn.probes == 32 && sn.dropped == 2 && sn.minute == 10 && sn.minutes == 1, "window counts");
        Require(c.Take(), "cap starts again in the new minute");
        Require(c.Roll(900, sn) && sn.probes == 0 && sn.dropped == 0 && sn.minute == 11 && sn.minutes == 4,
            "idle minutes: one window of 4 minutes");
        // an over-budget Take never gives the increment back.
        Counters w;
        w.Roll(60, sn);
        for (std::uint32_t i = 0; i < LinesPerMinute + 5; ++i)
            w.Take();
        w.Roll(120, sn);
        Require(sn.dropped == 5, "dropped = lines over the cap");
        std::uint32_t taken = 0;
        for (std::uint32_t i = 0; i < LinesPerMinute + 1; ++i)
            taken += w.Take() ? 1u : 0u;
        Require(taken == LinesPerMinute && w.logged.load() == LinesPerMinute + 1, "after the roll: the full budget again");
    }
    {
        // Roll -> Account -> Take (LogTransportBoardLine): the very first probe is counted, the probe that opens a minute
        // lands in that minute's window, not in the previous summary.
        Counters c;
        Snapshot sn;
        Require(!c.Roll(600, sn), "first probe: no summary");
        c.Account();
        Require(c.Take(), "first probe: line");
        Require(!c.Roll(659, sn), "same minute");
        c.Account();
        c.Take();
        Require(c.Roll(660, sn) && sn.probes == 2 && sn.minute == 10 && sn.minutes == 1, "first window: the first probe counted");
        c.Account();
        c.Take();
        Require(c.Roll(720, sn) && sn.probes == 1 && sn.minute == 11, "the boundary probe counted in its own minute");
    }
    {
        // concurrency: exactly the cap is logged, exactly one roller gets the summary.
        Counters c;
        Snapshot sn;
        c.Roll(60, sn);
        std::atomic<std::uint32_t> taken{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&c, &taken]() {
                for (int i = 0; i < 1000; ++i)
                {
                    c.Account();
                    if (c.Take())
                        taken.fetch_add(1);
                }
            });
        for (std::thread& thread : threads)
            thread.join();
        Require(taken.load() == LinesPerMinute, "exactly the cap is logged under concurrency");
        std::atomic<std::uint32_t> rolled{0};
        std::vector<std::thread> rollers;
        for (int t = 0; t < 8; ++t)
            rollers.emplace_back([&c, &rolled]() {
                Snapshot local;
                if (c.Roll(120, local))
                {
                    rolled.fetch_add(1);
                    Require(local.probes == 8000 && local.dropped == 8000 - LinesPerMinute, "every probe counted");
                }
            });
        for (std::thread& thread : rollers)
            thread.join();
        Require(rolled.load() == 1, "exactly one thread gets the minute's summary");
    }

    // ===== S3b-1 (AiPlayerbot.Path.TransportBoardFix) =====
    Require(BoardRetryMs == 5000u && StopCheckMs >= 2000u && StopCheckMs <= 5000u, "board throttle 5 s, stop test paced 2-5 s");
    Require(MaxExitFromDockYards == 40.0f && RecoverNearYards == 40.0f && SameDockYards == 1.0f, "exit / recovery radii");

    // Board gate: a known entry, not the run end, legacy 5 yd or the vessel at its stop near the dock.
    Require(BoardOpen(177233, false, true, false), "legacy near opens");
    Require(BoardOpen(177233, false, false, true), "vessel at its stop near the dock opens");
    Require(!BoardOpen(177233, false, false, false), "neither: closed");
    Require(!BoardOpen(0, false, true, true), "entry 0: never boarded with the fix");
    Require(!BoardOpen(177233, true, true, true), "run end (arrival side): never boarded");
    // Honest answer: aboard, or walking there in walk mode; a failed teleport boarding keeps waiting.
    Require(Boarded(true, true, false) && Boarded(true, false, false), "aboard");
    Require(!Boarded(false, true, false) && !Boarded(false, true, true), "teleport mode, nothing boarded: keep waiting");
    Require(Boarded(false, false, true) && !Boarded(false, false, false), "walk mode: walking there / not");

    // Stop test pacing: the stored answer for the same vessel and dock while younger than StopCheckMs, wrap-safe.
    {
        StopCheck c;
        Require(!StopCheckFresh(c, 177233, 1, -4211.15f, 3287.06f, 1000), "nothing stored: check now");
        NoteStopCheck(c, 177233, 1, -4211.15f, 3287.06f, 1000, true);
        Require(StopCheckFresh(c, 177233, 1, -4211.15f, 3287.06f, 1000 + StopCheckMs - 1) && c.answer, "within the pace: stored");
        Require(!StopCheckFresh(c, 177233, 1, -4211.15f, 3287.06f, 1000 + StopCheckMs), "at the pace: check again");
        Require(!StopCheckFresh(c, 176244, 1, -4211.15f, 3287.06f, 1500), "another vessel: check now");
        Require(!StopCheckFresh(c, 177233, 1, -4347.53f, 2430.49f, 1500), "another dock: check now");
        Require(!StopCheckFresh(c, 177233, 0, -4211.15f, 3287.06f, 1500), "another map: check now");
        NoteStopCheck(c, 177233, 1, 1.0f, 1.0f, 0xFFFFFF00u, false);
        Require(StopCheckFresh(c, 177233, 1, 1.0f, 1.0f, 0x00000100u) && !c.answer, "clock wrap within the pace");
    }

    // Board try throttle: one try per bot and ride per 5 s, another ride at once, wrap-safe.
    {
        BotState s;
        Require(BoardTryDue(s, 177233, 1000), "first try");
        Require(!BoardTryDue(s, 177233, 1000 + BoardRetryMs - 1), "same ride within 5 s: no try");
        Require(BoardTryDue(s, 176244, 2000), "another ride: try at once");
        Require(!BoardTryDue(s, 176244, 2001), "and then throttled");
        Require(BoardTryDue(s, 176244, 2000 + BoardRetryMs), "at 5 s: try again");
        BotState w;
        Require(BoardTryDue(w, 7, 0xFFFFF000u), "before the wrap");
        Require(!BoardTryDue(w, 7, 0x00000100u), "across the wrap within 5 s: throttled");
        Require(BoardTryDue(w, 7, 0x00002000u), "across the wrap after 5 s: try");
        BotState z;
        Require(BoardTryDue(z, 7, 0) && z.tryMs == 1u && !BoardTryDue(z, 7, 10), "a try at clock 0 is stored (1)");
    }

    // Points, exit usability: unset, (0,0,0), other map, 39 / 41 yd.
    {
        Point const dock = PointOf(1, -4347.53f, 2430.49f, 6.36f);
        Require(!ExitUsable(Point(), dock), "unset exit");
        Require(!ExitUsable(PointOf(1, 0.0f, 0.0f, 0.0f), dock), "(0,0,0) exit");
        Require(!ExitUsable(PointOf(0, -4347.53f, 2430.49f, 6.36f), dock), "exit on another map");
        Require(ExitUsable(PointOf(1, -4347.53f + 39.0f, 2430.49f, 6.0f), dock), "39 yd: usable");
        Require(!ExitUsable(PointOf(1, -4347.53f + 41.0f, 2430.49f, 6.0f), dock), "41 yd: unusable");
        Require(!ExitUsable(PointOf(1, -4347.53f, 2430.49f, 6.0f), Point()), "no dock: unusable");
        Require(PointUsable(dock) && !PointUsable(Point()) && !PointUsable(PointOf(0, 0.0f, 0.0f, 0.0f)), "usable points");
    }

    // Ride side / exit with the Feathermoon points: boarded at stop 622, arrival dock 625; vessel at 623 / 622.
    Tail feather;
    feather.arrival = PointOf(1, -4347.53f, 2430.49f, 6.36f);   // 625 Feathermoon Ferrydock
    feather.exit = PointOf(1, -4340.0f, 2420.0f, 6.0f);
    Ride const ride = RideOf(177233, 1, -4197.31f, 3286.63f, feather);
    Require(ride.set && ride.entry == 177233u && ride.arrival.set && ride.exit.set, "ride record with the run's tail");
    Require(RideSide(ride, 1, -4347.81f, 2444.37f) == ai::transport_edge::Side::To, "vessel at 623: arrival side");
    Require(ExitOpen(RideSide(ride, 1, -4347.81f, 2444.37f), true, false), "arrival side, legacy near: open");
    Require(ExitOpen(RideSide(ride, 1, -4347.81f, 2444.37f), false, true), "arrival side, at its stop: open");
    Require(!ExitOpen(RideSide(ride, 1, -4347.81f, 2444.37f), false, false), "arrival side, not at the dock: closed");
    Require(RideSide(ride, 1, -4197.31f, 3286.63f) == ai::transport_edge::Side::From, "vessel at 622: departure side");
    Require(!ExitOpen(RideSide(ride, 1, -4197.31f, 3286.63f), true, true), "departure side: never an exit");
    {
        Ride noArrival = ride;
        noArrival.arrival = Point();
        Require(RideSide(noArrival, 1, -4347.81f, 2444.37f) == ai::transport_edge::Side::Unknown, "no arrival: unknown");
        Require(!ExitOpen(RideSide(noArrival, 1, -4347.81f, 2444.37f), true, true), "unknown side: closed");
        Require(!ExitOpen(RideSide(Ride(), 1, -4347.81f, 2444.37f), true, true), "no ride: closed");
    }
    // Cross map: Grom'gol (map 0) -> Orgrimmar (map 1).
    {
        Tail t;
        t.arrival = PointOf(1, 1320.0f, -4650.0f, 53.0f);
        t.exit = PointOf(1, 1330.0f, -4645.0f, 53.0f);
        Ride const zep = RideOf(164871, 0, -12410.0f, 210.0f, t);
        Require(RideSide(zep, 1, 1300.0f, -4600.0f) == ai::transport_edge::Side::To, "vessel on map 1: arrival side");
        Require(RideSide(zep, 0, -12400.0f, 200.0f) == ai::transport_edge::Side::From, "vessel on map 0: departure side");
    }
    // Arrival point: same entry, transport point, within 1 yd.
    Require(IsArrival(ride, true, 177233, 1, -4347.53f, 2430.49f), "the arrival dock");
    Require(IsArrival(ride, true, 177233, 1, -4347.53f + 0.9f, 2430.49f), "0.9 yd: the arrival dock");
    Require(!IsArrival(ride, true, 176244, 1, -4347.53f, 2430.49f), "another entry");
    Require(!IsArrival(ride, false, 177233, 1, -4347.53f, 2430.49f), "a walk point");
    Require(!IsArrival(ride, true, 177233, 1, -4347.53f + 1.5f, 2430.49f), "1.5 yd away");
    Require(!IsArrival(ride, true, 177233, 0, -4347.53f, 2430.49f), "another map");
    Require(!IsArrival(Ride(), true, 177233, 1, -4347.53f, 2430.49f), "no ride record");

    // D2: a 0-object hop takes the adjacent ride (next first, previous second, none when they differ).
    Require(InheritEntry(177233, 0, 0) == 177233u, "own entry kept");
    Require(InheritEntry(177233, 176244, 164871) == 177233u, "own entry wins over neighbours");
    Require(InheritEntry(0, 177233, 0) == 177233u, "dock -> stop hop: the next ride");
    Require(InheritEntry(0, 0, 177233) == 177233u, "stop -> dock hop: the previous ride");
    Require(InheritEntry(0, 177233, 177233) == 177233u, "same ride on both sides");
    Require(InheritEntry(0, 177233, 176244) == 0u, "two different rides: no guess");
    Require(InheritEntry(0, 0, 0) == 0u, "no ride around: 0");
    // Review 18.10.: such a lone hop (no transport link next to it, e.g. the first or last link of a route that starts or
    // ends at a stop node, then a walk point) is walked, never a run of entry 0 that a bot waits on; a hop next to a ride or
    // a lift keeps its transport run.
    Require(WalkHop(0, false, false), "lone hop at a route edge / between walk links: walked");
    Require(!WalkHop(0, true, false) && !WalkHop(0, false, true) && !WalkHop(0, true, true), "hop next to a transport link: kept");
    Require(!WalkHop(177233, false, false), "a link with its own object: kept");

    // Review 18.10.: a walking bot boards only within BoardFromDockYards (2D, same map) of the handed dock; MinimalMove's
    // teleport steps (no walker point) are not gated.
    {
        Require(BoardFromDockYards == StopRadiusYards, "walker radius = the stop radius");
        Point const dock = PointOf(1, -4211.15f, 3287.06f, 5.0f);   // 624 dock
        Require(WalkerNearDock(Point(), dock), "no walker point (MinimalMove / aboard): no gate");
        Require(WalkerNearDock(PointOf(1, -4211.15f + 19.0f, 3287.06f, 5.0f), dock), "19 yd: boards");
        Require(WalkerNearDock(PointOf(1, -4211.15f, 3287.06f + 19.5f, 30.0f), dock), "19.5 yd (2D, z ignored): boards");
        Require(!WalkerNearDock(PointOf(1, -4211.15f + 21.0f, 3287.06f, 5.0f), dock), "21 yd: walks on");
        Require(!WalkerNearDock(PointOf(1, -4211.15f + 150.0f, 3287.06f, 5.0f), dock), "ReactDistance away: walks on");
        Require(!WalkerNearDock(PointOf(0, -4211.15f, 3287.06f, 5.0f), dock), "another map: walks on");
        Require(!WalkerNearDock(PointOf(1, -4211.15f, 3287.06f, 5.0f), Point()), "no dock: no boarding");
    }

    // Lost ride (OB-10 #1 / #3): only with a ride record; near the arrival dock or the exit vs a route search.
    Require(!RideLost(Ride(), false, 0), "no ride record: never lost (no recovery for follow / legacy passengers)");
    Require(!RideLost(Ride(), true, 177233), "aboard without a record: not lost");
    Require(!RideLost(ride, true, 177233), "aboard its vessel: not lost");
    Require(RideLost(ride, false, 0), "off the vessel: lost");
    Require(RideLost(ride, true, 176244), "on another vessel: lost");
    Require(RecoverNear(ride, 1, -4347.53f + 39.0f, 2430.49f), "39 yd from the arrival dock: near");
    Require(RecoverNear(ride, 1, -4340.0f + 39.0f, 2420.0f), "39 yd from the exit: near");
    Require(!RecoverNear(ride, 1, -4347.53f + 60.0f, 2430.49f), "60 yd: search");
    Require(!RecoverNear(ride, 0, -4347.53f, 2430.49f), "another map: search");
    Require(!RecoverNear(Ride(), 1, -4347.53f, 2430.49f), "no record: never near");
    Require(std::string(RecoveryName(Recovery::Near)) == "near" && std::string(RecoveryName(Recovery::Search)) == "search" &&
            std::string(RecoveryName(Recovery::NoRoute)) == "noroute",
        "recovery names");
    Require(FormatRecovery(42, ride, Recovery::Near, 1, -4330.0f, 2430.5f) ==
            "[TransportBoard] state=stranded bot=42 ride=177233 kind=near at_map=1 at_x=-4330.0 at_y=2430.5 arrival_map=1 "
            "arrival_x=-4347.5 arrival_y=2430.5 d_arrival=17.5",
        "stranded line");
    Require(FormatRecovery(7, ride, Recovery::NoRoute, 0, 1.0f, 2.0f) ==
            "[TransportBoard] state=stranded bot=7 ride=177233 kind=noroute at_map=0 at_x=1.0 at_y=2.0 arrival_map=1 "
            "arrival_x=-4347.5 arrival_y=2430.5 d_arrival=-1.0",
        "stranded line on another map");
    {
        // Recovery counters: counted after the roll, reported in the summary window, reset by it.
        Counters c;
        Snapshot sn;
        c.Roll(600, sn);
        c.AccountRecovery(Recovery::Near);
        c.AccountRecovery(Recovery::Near);
        c.AccountRecovery(Recovery::Search);
        c.AccountRecovery(Recovery::NoRoute);
        Require(c.Roll(660, sn) && sn.recoverNear == 2 && sn.recoverSearch == 1 && sn.recoverNoRoute == 1 && sn.probes == 0,
            "recoveries in the window");
        Require(c.Roll(720, sn) && sn.recoverNear == 0 && sn.recoverSearch == 0 && sn.recoverNoRoute == 0, "reset by the roll");
    }

    // Flow model (review 1 HIGH-1/2, M-4) as a walk through the predicates.
    {
        // (i) boarded as minimal (ride record from the run's tail), detailed aboard: the exit only at the stored arrival.
        BotState s;
        s.pending = feather;
        s.ride = RideOf(177233, 1, -4197.31f, 3286.63f, s.pending);
        Require(!RideLost(s.ride, true, 177233), "aboard: the record drives the ride");
        Require(!ExitOpen(RideSide(s.ride, 1, -4197.31f, 3286.63f), true, false), "still at the departure stop: no exit");
        Require(ExitOpen(RideSide(s.ride, 1, -4347.81f, 2444.37f), false, true), "at the arrival stop: exit");
        Require(ExitUsable(s.ride.exit, s.ride.arrival), "the stored exit is used");
        // (ii) a re-planned path [prepath, ride, dockB, W2] finds dockB at index > 0; a path without it is rebuilt.
        struct P { bool transport; std::uint32_t entry; std::uint32_t map; float x, y; };
        P const replanned[] = {{false, 0, 1, -4300.0f, 2900.0f}, {true, 177233, 1, -4347.81f, 2444.37f},
            {true, 177233, 1, -4347.53f, 2430.49f}, {false, 0, 1, -4340.0f, 2420.0f}};
        int found = -1;
        for (int i = 0; i < 4 && found < 0; ++i)
            if (IsArrival(s.ride, replanned[i].transport, replanned[i].entry, replanned[i].map, replanned[i].x, replanned[i].y))
                found = i;
        Require(found == 2, "re-planned path: the arrival dock found at index 2");
        P const other[] = {{false, 0, 1, -4300.0f, 2900.0f}, {false, 0, 1, -4000.0f, 2000.0f}};
        found = -1;
        for (int i = 0; i < 2 && found < 0; ++i)
            if (IsArrival(s.ride, other[i].transport, other[i].entry, other[i].map, other[i].x, other[i].y))
                found = i;
        Require(found == -1, "a path without the arrival dock: rebuilt from the record");
        // (iii) fell off mid-sea: lost, not near (route search); fell off at the pier: near (continue after the run).
        Require(RideLost(s.ride, false, 0) && !RecoverNear(s.ride, 1, -4280.0f, 2850.0f), "fell off mid-sea: search");
        Require(RecoverNear(s.ride, 1, -4350.0f, 2440.0f), "fell off at the arrival pier: near");
        // A boarding try from the arrival side (run end) is refused; the honest answer waits.
        Require(!BoardOpen(177233, true, true, true) && !Boarded(false, true, false), "arrival side: refused, waiting");
    }

    std::cout << "transport_board_policy_tests passed\n";
    return 0;
}
