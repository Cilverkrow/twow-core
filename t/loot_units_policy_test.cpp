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

    // Owner 2026-09-28 (#429 hotfix 7.3): dungeon target 2..4, max 4, at least 1 BoE while room.
    Check(FunserverDungeonBoeUnits(1, 2, 4, 1) == 1 && FunserverDungeonBoeUnits(1, 4, 4, 1) == 3, "1 own -> 1..3 BoE");
    Check(FunserverDungeonBoeUnits(2, 2, 4, 1) == 1 && FunserverDungeonBoeUnits(2, 4, 4, 1) == 2, "2 own -> 1..2 BoE");
    Check(FunserverDungeonBoeUnits(3, 2, 4, 1) == 1 && FunserverDungeonBoeUnits(3, 4, 4, 1) == 1, "3 own -> 1 BoE");
    Check(FunserverDungeonBoeUnits(4, 4, 4, 1) == 0 && FunserverDungeonBoeUnits(6, 2, 4, 1) == 0, "4+ own -> no BoE");
    Check(FunserverDungeonBoeUnits(0, 2, 4, 1) == 2 && FunserverDungeonBoeUnits(0, 4, 4, 1) == 4, "no own drop -> T BoE");
    Check(FunserverDungeonBoeUnits(1, 3, 4, 0) == 2 && FunserverDungeonBoeUnits(3, 2, 4, 0) == 0, "minBoe 0 fills to T only");

    // Raids: fill normal loot to the map target, set pieces on top, never above 16 in total.
    Check(FunserverRaidFillUnits(1, 8, 3, 16) == 7, "MC target 8, 1 normal + 2 set on corpse -> 7 more");
    Check(FunserverRaidFillUnits(5, 4, 7, 16) == 0, "normal drops already above target -> none");
    Check(FunserverRaidFillUnits(0, 8, 12, 16) == 4, "hard cap 16 wins over the target");
    Check(FunserverRaidFillUnits(0, 8, 16, 16) == 0, "full corpse -> none");

    auto ranges = FunserverParseMapRanges("409:6-8,469:4-6, 814:2-4,bad,531:6-4,533:3-5x,819:3-5");
    Check(ranges.size() == 4, "four valid map ranges");
    Check(ranges.count(409) && ranges[409].min == 6 && ranges[409].max == 8, "MC 6..8");
    Check(ranges.count(814) && ranges[814].min == 2 && ranges[814].max == 4, "Kara 814 2..4 with blank");
    Check(!ranges.count(531) && !ranges.count(533), "min > max and trailing junk skipped");
    Check(FunserverParseMapRanges("").empty(), "empty string = off");

    if (failures)
        return 1;
    std::cout << "LOOT_UNITS_POLICY=PASS\n";
    return 0;
}
