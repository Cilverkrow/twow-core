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

    std::cout << "transport_board_policy_tests passed\n";
    return 0;
}
