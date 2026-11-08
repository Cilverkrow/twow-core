#include "RescueHubPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// twow-repo#575 RG-a (AiPlayerbot.QuestRescue.HomeHubs): the anchor table of RescueHubPolicy.h - rows per race and level band
// (OB-50 innkeepers, SUMS 7c52ff88), the R1 gate for next_zone rows, names and counters.
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

std::uint32_t FirstEntry(std::uint32_t race, std::uint32_t level)
{
    ai::rescue_hub::Row const* row = ai::rescue_hub::RowFor(race, level);
    return row ? row->entries[0] : 0;
}
}

int main()
{
    using namespace ai::rescue_hub;

    // 1. Config loader: an unknown mode is off.
    {
        Require(ClampMode(0) == 0 && ClampMode(1) == 1 && ClampMode(2) == 2 && ClampMode(3) == 3, "modes 0-3 pass");
        Require(ClampMode(4) == 0 && ClampMode(-1) == 0, "an unknown mode acts like 0");
    }

    // 2. Own start-zone hub below L5, for every standard race.
    {
        Require(FirstEntry(1, 1) == 295, "human L1: Goldshire (Farley)");
        Require(FirstEntry(3, 4) == 1247 && FirstEntry(7, 4) == 1247, "dwarf / gnome L4: Kharanos (Belm)");
        Require(FirstEntry(4, 2) == 6736, "night elf L2: Dolanaar (Keldamyr)");
        Require(FirstEntry(2, 3) == 6928 && FirstEntry(8, 3) == 6928, "orc / troll: Razor Hill (Grosk)");
        Require(FirstEntry(5, 1) == 5688, "undead: Brill (Renee), not Undercity");
        Require(FirstEntry(6, 4) == 6747, "tauren: Bloodhoof (Kauth), not Thunder Bluff (Pala 6746)");
        for (std::uint32_t race : { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u })
        {
            Row const* row = RowFor(race, 4);
            Require(row && row->step == Step::OwnHub && row->minDistance == OwnHubMinDistance, "L4: own_hub with the short distance");
        }
    }

    // 3. Next zone: night elf / undead L5-10 only; the other races go back to legacy at L5.
    {
        Row const* ne = RowFor(4, 5);
        Require(ne && ne->step == Step::NextZone && ne->entries[0] == 6737 && ne->minDistance == NextZoneMinDistance, "night elf L5: Auberdine");
        Require(FirstEntry(4, 10) == 6737, "night elf L10: Auberdine");
        Require(FirstEntry(5, 7) == 6739 && RowFor(5, 7)->step == Step::NextZone, "undead L7: The Sepulcher");
        Require(!RowFor(4, 11) && !RowFor(5, 11), "L11: legacy (inns by level)");
        for (std::uint32_t race : { 1u, 2u, 3u, 6u, 7u, 8u })
            Require(!RowFor(race, 5), "L5 of a race whose faction has another start on its continent: legacy");
    }

    // 4. High elf (OB-00 08.11.): own hub up to L9 (two innkeepers), no next-zone row; goblin: no row (island rules).
    {
        Row const* he = RowFor(10, 9);
        Require(he && he->step == Step::OwnHub && he->entries[0] == 80227 && he->entries[1] == 61831, "high elf L9: Brinthilien / Alah'Thalas");
        Require(!RowFor(10, 10), "high elf L10: legacy (cross-continent rule)");
        for (std::uint32_t level = 1; level <= 60; ++level)
            Require(!RowFor(9, level), "goblin: never a row");
        Require(!RowFor(0, 1) && !RowFor(11, 1), "unknown race: no row");
        Require(!RowFor(1, 0), "level 0: no row");
    }

    // 5. The R1 gate holds back next_zone rows only.
    {
        Require(Usable(RowFor(4, 6), false) && !Usable(RowFor(4, 6), true), "next_zone gated by R1");
        Require(Usable(RowFor(4, 2), true), "own_hub stays in the start zone: R1 does not gate it");
        Require(!Usable(nullptr, false), "no row: not usable");
    }

    // 6. Every row is well formed.
    {
        for (Row const& row : Table)
        {
            Require(row.minLevel >= 1 && row.minLevel <= row.maxLevel && row.maxLevel <= 10, "level band inside 1-10");
            Require(row.entries[0] != 0, "a row has an entry");
            Require(row.step == Step::OwnHub || row.step == Step::NextZone, "a row is own_hub or next_zone");
            for (Row const& other : Table)
                if (&other != &row && other.race == row.race)
                    Require(other.maxLevel < row.minLevel || other.minLevel > row.maxLevel, "bands of a race do not overlap");
        }
    }

    // 7. Names and counters.
    {
        Require(std::strcmp(StepName(Step::OwnHub), "own_hub") == 0 && std::strcmp(StepName(Step::NextZone), "next_zone") == 0 &&
            std::strcmp(StepName(Step::Legacy), "legacy") == 0 && std::strcmp(StepName(Step::Count), "unknown") == 0, "step names");
        Counters counters;
        counters.Hub(Step::OwnHub);
        counters.Hub(Step::OwnHub);
        counters.Hub(Step::NextZone);
        counters.Hub(Step::Count);
        Require(counters.TakeHub(Step::OwnHub) == 2 && counters.TakeHub(Step::OwnHub) == 0, "take resets");
        Require(counters.TakeHub(Step::NextZone) == 1, "next_zone counted");
        Require(counters.LogDue(1'800'000'000) && !counters.LogDue(1'800'000'030) && counters.LogDue(1'800'000'060), "one minute line per minute");
    }

    std::cout << "rescue_hub_policy_tests: ok\n";
    return 0;
}
