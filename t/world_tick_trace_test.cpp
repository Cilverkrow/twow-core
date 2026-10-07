// twow-repo#541: [WorldTick] / [MapTick] aggregation (src/game/WorldTickTrace.h), no server needed.
#include "../src/game/WorldTickTrace.h"

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
}

int main()
{
    using namespace world_tick;

    // Phases: Rest is the remainder of the update time, Sleep is passed in.
    PhaseWindow w;
    w.Add(Sessions, 2000);
    w.Add(WaitContinents, 30000);
    w.Add(Results, 1000);
    w.EndTick(40000, 10000);
    Require(w.Ticks() == 1, "one tick recorded");
    Require(w.samples[Rest][0] == 7000, "rest = update - measured phases");
    Require(w.samples[Sleep][0] == 10000, "sleep kept");
    Require(w.samples[Sessions][0] == 2000 && w.samples[Teleports][0] == 0, "phases per tick, unused phases 0");
    Require(w.tick[Sessions] == 0, "tick accumulators reset");

    // More measured than the update time (clock granularity): rest never negative.
    w.Add(MapsPre, 5000);
    w.EndTick(4000, 0);
    Require(w.samples[Rest][1] == 0, "rest clamped at 0");

    // Same phase added twice in one tick (teleports before and after the map update).
    w.Add(Teleports, 300);
    w.Add(Teleports, 200);
    w.EndTick(1000, 0);
    Require(w.samples[Teleports][2] == 500, "phase summed within a tick");

    std::vector<uint32_t> s = { 10, 20, 30, 40, 1000 };
    PhaseStat const st = Summarize(s);
    Require(st.avgUs == 220 && st.maxUs == 1000 && st.p95Us == 1000, "avg / p95 / max");
    std::vector<uint32_t> empty;
    Require(Summarize(empty).avgUs == 0, "empty window");

    // Regions: barrier waste = slowest - average, slowest region counted.
    RegionWindow r;
    uint64_t const a = RegionWindow::Key(0, 1), b = RegionWindow::Key(0, 2), c = RegionWindow::Key(1, 1);
    r.AddTick({ { a, 10000 }, { b, 30000 }, { c, 20000 } });
    Require(r.Ticks() == 1 && r.waste[0] == 10000 && r.slowest[0] == 30000, "waste = max - avg");
    Require(r.slowestCount[b] == 1 && r.byRegion[a].size() == 1, "slowest region and per-region samples");
    r.AddTick({});
    Require(r.Ticks() == 1, "a tick without regions is not recorded");
    Require(RegionWindow::Key(1, 7) == ((uint64_t(1) << 32) | 7), "region key");

    w.Clear();
    r.Clear();
    Require(w.Ticks() == 0 && r.Ticks() == 0, "windows cleared");
    Require(std::string(PhaseName(WaitContinents)) == "wait_continents", "phase names");

    std::cout << "world_tick_trace_test passed\n";
    return 0;
}
