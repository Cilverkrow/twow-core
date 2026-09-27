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

    // Owner 2026-09-27 (e, d): unique items and pool items at most once per kill.
    Check(Near(FunserverUnitWeightCapped(25.0f, 10.0f, 0, 0.75f, FUNSERVER_UNIQUE_MAX_COPIES), 250.0f), "unique first copy drawable");
    Check(FunserverUnitWeightCapped(25.0f, 10.0f, 1, 0.75f, FUNSERVER_UNIQUE_MAX_COPIES) == 0.0f, "unique second copy blocked");
    Check(FunserverUnitWeightCapped(1.0f, 1.0f, 1, 0.75f, FUNSERVER_POOL_MAX_COPIES) == 0.0f, "pool item only once");
    Check(Near(FunserverUnitWeightCapped(20.0f, 10.0f, 2, 0.75f, 0), 112.5f), "no cap keeps the decay");

    // Owner 2026-09-27 (a): world pool item level >= own max - 5; no own floor items = no bound.
    Check(!FunserverBoeItemLevelMatch(66, 88, 5), "T1 belt (66) not in a T3.5 pool (88)");
    Check(FunserverBoeItemLevelMatch(83, 88, 5) && !FunserverBoeItemLevelMatch(82, 88, 5), "margin edge inclusive");
    Check(FunserverBoeItemLevelMatch(66, 71, 5), "T1 pool stays for MC-level tables");
    Check(FunserverBoeItemLevelMatch(40, 0, 5), "no own floor items -> level window only");

    if (failures)
        return 1;
    std::cout << "LOOT_UNITS_POLICY=PASS\n";
    return 0;
}
