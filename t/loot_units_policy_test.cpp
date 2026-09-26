// twow-repo#323 rules for fixed funserver loot units.
#include "../src/game/FunserverLootUnits.h"

#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(bool ok, char const* label)
    {
        if (!ok)
        {
            std::cerr << "FAIL: " << label << "\n";
            ++failures;
        }
    }

    bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}

int main()
{
    // Diminishing: -25 % per copy already dropped, multiplicative.
    Check(Near(FunserverUnitWeight(20.0f, 10.0f, 0, 0.75f), 200.0f), "blue 20% x10, first copy");
    Check(Near(FunserverUnitWeight(20.0f, 10.0f, 1, 0.75f), 150.0f), "second copy -25%");
    Check(Near(FunserverUnitWeight(20.0f, 10.0f, 2, 0.75f), 112.5f), "third copy -25% again");
    Check(FunserverUnitWeight(0.0f, 10.0f, 0, 0.75f) == 0.0f, "zero chance never drawn");
    Check(FunserverUnitWeight(20.0f, 0.0f, 0, 0.75f) == 0.0f, "zero weight never drawn");
    Check(FunserverUnitWeight(20.0f, 10.0f, 1, 0.0f) == 0.0f, "decay 0 forbids repeats");

    // Quality index: poor..legendary, artifact uses legendary.
    Check(FunserverLootQualityIndex(0) == 0 && FunserverLootQualityIndex(5) == 5, "quality index range");
    Check(FunserverLootQualityIndex(6) == 5, "artifact -> legendary weight");

    // BoE pool covers what the own table cannot at the floor.
    Check(FunserverBoePoolShortfall(8, 5) == 3, "dungeon 8 units, 5 blue items -> 3 from pool");
    Check(FunserverBoePoolShortfall(6, 9) == 0, "rare with rich table needs no pool");
    Check(FunserverBoePoolShortfall(16, 0) == 16, "no floor items -> all from pool");

    // Level window +-4 (owner rule).
    Check(FunserverBoeLevelMatch(56, 60, 4) && FunserverBoeLevelMatch(60, 56, 4), "window edges inclusive");
    Check(!FunserverBoeLevelMatch(55, 60, 4) && !FunserverBoeLevelMatch(61, 56, 4), "outside window");

    if (failures)
        return 1;
    std::cout << "LOOT_UNITS_POLICY=PASS\n";
    return 0;
}
