#include "AreaLevelPolicy.h"

#include <cstdlib>
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

    std::cout << "area_level_policy_tests passed\n";
    return 0;
}
