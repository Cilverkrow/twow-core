#include "PathUnitlessBotPolicy.h"

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

bool Is(char const* actual, char const* expected)
{
    return actual && expected && std::strcmp(actual, expected) == 0;
}
}

// twow-repo#592 (PATH-02): pure policy of AiPlayerbot.Perf.PathGenUnitless and the [PathUnitlessBot] counters. The
// integration (generateAll, GetFishLocations, TravelNodeRoute::buildPath, getPathStepFrom, the report) is pinned by
// t/path_unitless_592_source_contract_tests.cmake.
int main()
{
    using namespace ai::path_unitless_bot;

    // Counters start at zero (nothing is reported until a rare case happens).
    Require(transportRejects.load() == 0 && routeIncomplete.load() == 0, "rare counters start at zero");
    Require(modeUnit.load() == 0 && modeMap.load() == 0 && fishSkipped.load() == 0, "mode and fish counters start at zero");

    // Gate: everything on -> may run.
    Require(GenerateSkipCause(true, true, true) == nullptr, "switch, core map mode and preload -> generator may run");
    // Each input alone closes it, the switch first, then the core switch, then preload.
    Require(Is(GenerateSkipCause(false, true, true), "switch_off"), "switch off");
    Require(Is(GenerateSkipCause(false, false, false), "switch_off"), "switch off wins over everything");
    Require(Is(GenerateSkipCause(true, false, true), "core_unitless_off"), "PathFinder.Unitless = 0");
    Require(Is(GenerateSkipCause(true, false, false), "core_unitless_off"), "core switch before preload");
    Require(Is(GenerateSkipCause(true, true, false), "preload_off"), "Terrain.Preload.Continents = 0");
    Require(Is(TilesIncompleteCause, "tiles_incomplete"), "tile cause name");

    // Tiles: every .mmtile in the navmesh, and at least one file (no mmaps directory is not complete).
    Require(TilesComplete(1813, 1813), "all tiles loaded");
    Require(TilesComplete(1814, 1813), "more tiles in the mesh than files counted");
    Require(!TilesComplete(1812, 1813), "one tile missing");
    Require(!TilesComplete(0, 1813), "nothing loaded (no preload)");
    Require(!TilesComplete(0, 0), "no files at all");
    Require(!TilesComplete(5, 0), "tiles without files");

    std::cout << "path_unitless_592_policy tests passed\n";
    return 0;
}
