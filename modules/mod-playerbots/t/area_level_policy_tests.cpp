#include "AreaLevelPolicy.h"

#include <cstdlib>
#include <iostream>
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
}

int main()
{
    using namespace ai::area_level;

    // The live store: 1481 records, ids up to 5735 (end 5736).
    Require(AreaIdEnd(5736, 1481) == 5736, "the loop reaches the Turtle areas above the record count");
    Require(AreaIdEnd(1000, 1481) == 1481, "never below the record count");

    Require(MayUseCreatureLevels(true), "creature levels while the area levels load at startup");
    Require(!MayUseCreatureLevels(false), "#416: no world scan at runtime");

    CreatureLevels levels;
    levels.Add(5643, 50);
    levels.Add(5643, 54);
    levels.Add(221, 0);
    Require(levels.Average(5643) == 52, "average of the creatures in the area");
    Require(levels.Average(221) == 1, "an area with only level-0 creatures counts as 1");
    Require(levels.Average(999) == 0, "no creature in the area: 0");
    Require(levels.Size() == 2, "two areas filled");

    // #541 (C1): the frozen table.
    std::vector<std::pair<uint32_t, int32_t>> const loaded = {{12, 5}, {5225, 7}, {40, -2}, {41, -1}, {42, 0}, {9000, 60}};
    std::vector<int32_t> const table = FreezeLevels(loaded, 5736);
    Require(table.size() == 5736, "one slot per area id");
    int32_t level = 99;
    Require(FrozenLookup(table, 12, level) && level == 5, "a loaded level");
    Require(FrozenLookup(table, 5225, level) && level == 7, "a Turtle area above the record count");
    Require(FrozenLookup(table, 40, level) && level == -2, "-2 (no area entry) is kept");
    Require(FrozenLookup(table, 41, level) && level == -1, "-1 (no level) is kept");
    Require(FrozenLookup(table, 42, level) && level == 0, "0 is a value, not unset");
    level = 99;
    Require(!FrozenLookup(table, 13, level) && level == 99, "an id the load did not fill takes the locked path");
    Require(!FrozenLookup(table, 9000, level), "ids past the end are dropped and take the locked path");
    Require(!FrozenLookup(table, 0xFFFFFFFFu, level), "no out-of-range read");
    Require(!FrozenLookup(std::vector<int32_t>(), 12, level), "an empty table knows nothing");

    std::cout << "area_level_policy_tests passed\n";
    return 0;
}
