#include "DangerMapPolicy.h"

#include <cstdlib>
#include <iostream>
#include <thread>

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

ai::danger_map::Death DeathOf(std::uint32_t time, std::uint32_t victim, std::uint8_t killerLevel, std::uint8_t victimLevel = 5)
{
    ai::danger_map::Death death;
    death.time = time;
    death.victim = victim;
    death.killerLevel = killerLevel;
    death.victimLevel = victimLevel;
    return death;
}
}

int main()
{
    using namespace ai::danger_map;
    Params const params;  // 100 yd, 7200 s, 3 deaths, +3 levels, 20 samples
    std::uint32_t const now = 100000;

    // Cell rule: three different bots killed by a level 10 mob.
    std::vector<Death> riverpaw{ DeathOf(now - 10, 1, 10), DeathOf(now - 20, 2, 10), DeathOf(now - 30, 3, 10) };
    Require(DangerousKillerLevel(riverpaw, now, 5, params) == 10, "three bots killed by a +5 mob mark the cell");
    Require(DangerousKillerLevel(riverpaw, now, 7, params) == 10, "exactly +3 still counts");
    Require(DangerousKillerLevel(riverpaw, now, 8, params) == 0, "a mob only +2 above is not a danger");

    std::vector<Death> oneBotLoop{ DeathOf(now - 10, 1, 10), DeathOf(now - 20, 1, 10), DeathOf(now - 30, 1, 10) };
    Require(DangerousKillerLevel(oneBotLoop, now, 5, params) == 0, "one bot dying in a loop is #138's case");

    std::vector<Death> old{ DeathOf(now - 7200, 1, 10), DeathOf(now - 20, 2, 10), DeathOf(now - 30, 3, 10) };
    Require(DangerousKillerLevel(old, now, 5, params) == 0, "deaths outside the window do not count");

    Params off = params;
    off.minDeaths = 0;
    Require(DangerousKillerLevel(riverpaw, now, 5, off) == 0, "MinDeaths 0 disables the rule");

    // Cells and line sampling.
    Require(CellOf(0, -1.0f, 250.0f, 100.0f) == Cell{ 0, -1, 2 }, "negative coordinates floor");
    std::vector<Cell> line = LineCells(1, 0.0f, 0.0f, 1000.0f, 0.0f, params);
    Require(line.size() == 9, "1000 yd east: cells 2..10, own cell and neighbour skipped");
    Require(line.front() == Cell{ 1, 2, 0 } && line.back() == Cell{ 1, 10, 0 }, "line runs from bot to target");
    Params few = params;
    few.lineSamples = 3;
    Require(LineCells(1, 0.0f, 0.0f, 1000.0f, 0.0f, few).size() <= 3, "samples are bounded");
    Require(LineCells(1, 0.0f, 0.0f, 50.0f, 0.0f, params).empty(), "short routes have no line samples");

    // Shared map: record, query destination, route and neighbours.
    DangerMap map;
    for (std::uint32_t victim = 1; victim <= 3; ++victim)
        map.Record(1, 550.0f, 50.0f, DeathOf(now - victim, victim, 10), params);

    DangerMap::Hit hit = map.Query(1, 0.0f, 0.0f, 550.0f, 50.0f, 5, now, params);
    Require(hit.cells == 1 && hit.worstKillerLevel == 10, "destination cell is dangerous");
    Require(map.Query(1, 0.0f, 0.0f, 1000.0f, 0.0f, 5, now, params).cells == 1, "route through the cell is dangerous");
    Require(map.Query(1, 0.0f, 0.0f, 650.0f, 150.0f, 5, now, params).cells == 1, "neighbour of the destination counts");
    Require(map.Query(1, 0.0f, 0.0f, 0.0f, 1000.0f, 5, now, params).cells == 0, "another direction is safe");
    Require(map.Query(2, 0.0f, 0.0f, 550.0f, 50.0f, 5, now, params).cells == 0, "other maps are separate");
    Require(map.Query(1, 0.0f, 0.0f, 550.0f, 50.0f, 9, now, params).cells == 0, "higher-level bots are not deterred");
    Require(map.Query(1, 500.0f, 0.0f, 550.0f, 50.0f, 5, now, params).cells == 0, "a bot already in the cell is not stranded");
    Require(map.Query(1, 0.0f, 0.0f, 550.0f, 50.0f, 5, now + 7200, params).cells == 0, "danger expires");

    auto const [cells, deaths] = map.Prune(now + 7200, params);
    Require(cells == 0 && deaths == 0, "prune drops expired cells");

    // Per-cell bound.
    DangerMap bounded;
    for (std::uint32_t i = 0; i < 40; ++i)
        bounded.Record(0, 0.0f, 0.0f, DeathOf(now, i, 20), params);
    Require(bounded.Prune(now, params).second == DangerMap::MaxDeathsPerCell, "a cell keeps at most 16 deaths");

    // Concurrent writers and readers (map update thread pools).
    DangerMap shared;
    std::vector<std::thread> threads;
    for (std::uint32_t t = 0; t < 4; ++t)
        threads.emplace_back([&, t]
        {
            for (std::uint32_t i = 0; i < 2000; ++i)
            {
                shared.Record(0, float(i % 50) * 100.0f, float(t) * 100.0f, DeathOf(now, t * 10000 + i, 20), params);
                shared.Query(0, 0.0f, 0.0f, 5000.0f, 300.0f, 5, now, params);
            }
        });
    for (std::thread& thread : threads)
        thread.join();
    Require(shared.Query(0, 0.0f, 0.0f, 5000.0f, 0.0f, 5, now, params).cells > 0, "concurrent use keeps the map consistent");

    return 0;
}
