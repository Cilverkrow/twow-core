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
    ai::rescue_hub::Row const* row = ai::rescue_hub::RowFor(race, level, true);
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
            Row const* row = RowFor(race, 4, true);
            Require(row && row->step == Step::OwnHub && row->minDistance == OwnHubMinDistance, "L4: own_hub with the short distance");
        }
    }

    // 3. Next zone: night elf / undead L5-10 only; the other races go back to legacy at L5.
    {
        Row const* ne = RowFor(4, 5, true);
        Require(ne && ne->step == Step::NextZone && ne->entries[0] == 6737 && ne->minDistance == NextZoneMinDistance, "night elf L5: Auberdine");
        Require(FirstEntry(4, 10) == 6737, "night elf L10: Auberdine");
        Require(FirstEntry(5, 7) == 6739 && RowFor(5, 7, true)->step == Step::NextZone, "undead L7: The Sepulcher");
        Require(!RowFor(4, 11, true) && !RowFor(5, 11, true), "L11: legacy (inns by level)");
        for (std::uint32_t race : { 1u, 2u, 3u, 6u, 7u, 8u })
            Require(!RowFor(race, 5, true), "L5 of a race whose faction has another start on its continent: legacy");
    }

    // 4. High elf (OB-00 08.11.): own hub up to L9 (two innkeepers), no next-zone row; goblin: no row (island rules).
    {
        Row const* he = RowFor(10, 9, true);
        Require(he && he->step == Step::OwnHub && he->entries[0] == 80227 && he->entries[1] == 61831, "high elf L9: Brinthilien / Alah'Thalas");
        Require(!RowFor(10, 10, true), "high elf L10: legacy (cross-continent rule)");
        for (std::uint32_t level = 1; level <= 60; ++level)
            Require(!RowFor(9, level, true), "goblin: never a row");
        Require(!RowFor(0, 1, true) && !RowFor(11, 1, true), "unknown race: no row");
        Require(!RowFor(1, 0, true), "level 0: no row");
    }

    // 4b. Without CustomRaceStartZones (SZM adoption review 10.11.2026): no high-elf row (proxy start Northshire, legacy targets);
    //     goblins still none; every other race gets the same row as with custom starts.
    {
        for (std::uint32_t level = 1; level <= 60; ++level)
        {
            Require(!RowFor(10, level, false), "high elf without custom starts: no row");
            Require(!RowFor(9, level, false), "goblin without custom starts: no row");
            for (std::uint32_t race : { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u })
                Require(RowFor(race, level, false) == RowFor(race, level, true), "standard races: custom starts change nothing");
        }
    }

    // 5. The R1 gate holds back next_zone rows only.
    {
        Require(Usable(RowFor(4, 6, true), false) && !Usable(RowFor(4, 6, true), true), "next_zone gated by R1");
        Require(Usable(RowFor(4, 2, true), true), "own_hub stays in the start zone: R1 does not gate it");
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

    // 6b. #703 item 2 (HomeHubs.L5OwnHub): L5-10 own-hub rows, seen only with the flag.
    {
        // Without the flag: the lookup is the main table, for every race and level (= today's rows, pin).
        for (std::uint32_t race = 0; race <= 11; ++race)
            for (std::uint32_t level = 0; level <= 60; ++level)
                for (bool custom : { false, true })
                    Require(RowFor(race, level, custom, false) == RowFor(race, level, custom), "flag off: the old lookup, row for row");
        // With the flag: human / dwarf / gnome / orc / troll / tauren L5-10 go to their own start-zone hub.
        struct Expect { std::uint32_t race; std::uint32_t entry; };
        for (Expect e : { Expect{ 1, 295 }, Expect{ 3, 1247 }, Expect{ 7, 1247 }, Expect{ 2, 6928 }, Expect{ 8, 6928 }, Expect{ 6, 6747 } })
        {
            for (std::uint32_t level = 5; level <= 10; ++level)
            {
                Row const* row = RowFor(e.race, level, true, true);
                Require(row && row->step == Step::OwnHub && row->entries[0] == e.entry && row->entries[1] == 0 &&
                    row->minDistance == OwnHubMinDistance, "flag on, L5-10: own hub with the short distance");
                Require(IsL5OwnHubRow(row), "flag on, L5-10: a row of the L5 table");
                Require(RowFor(e.race, level, false, true) == row, "custom starts change nothing for these races");
                Require(Usable(row, true) && Usable(row, false), "own hub: R1 does not gate it");
            }
            Require(RowFor(e.race, 4, true, true) == RowFor(e.race, 4, true) && !IsL5OwnHubRow(RowFor(e.race, 4, true, true)),
                "flag on, L4: the unchanged L1-4 row");
            Require(RowFor(e.race, 4, true, true)->entries[0] == e.entry, "the L5 row uses the same anchor as the L1-4 row");
            Require(!RowFor(e.race, 11, true, true), "flag on, L11: legacy");
        }
        // Night elf / undead keep next_zone; high elf and goblin unchanged; no main-table row is an L5 row.
        for (std::uint32_t level = 1; level <= 60; ++level)
        {
            for (std::uint32_t race : { 4u, 5u, 9u, 10u })
                for (bool custom : { false, true })
                    Require(RowFor(race, level, custom, true) == RowFor(race, level, custom), "night elf / undead / goblin / high elf: flag changes nothing");
        }
        for (Row const& row : Table)
            Require(!IsL5OwnHubRow(&row), "a main-table row is not an L5 row");
        Require(!IsL5OwnHubRow(nullptr), "no row: not an L5 row");
        // Well formed: bands L5-10, own hub, no overlap with the main table or each other.
        for (Row const& row : L5OwnHubTable)
        {
            Require(row.minLevel == 5 && row.maxLevel == 10 && row.step == Step::OwnHub && row.entries[0] != 0, "L5 row: 5-10 own hub");
            for (Row const& other : Table)
                if (other.race == row.race)
                    Require(other.maxLevel < row.minLevel || other.minLevel > row.maxLevel, "no overlap with the main table");
            for (Row const& other : L5OwnHubTable)
                if (&other != &row)
                    Require(other.race != row.race, "one L5 row per race");
        }
    }

    // 6c. #703 item 2 (OB-10 review point 1): the outcome. at_hub only for an L5 row with spawns that are all near.
    {
        Row const* l5 = RowFor(1, 6, true, true);
        Row const* l1 = RowFor(1, 3, true);
        Row const* ne = RowFor(4, 6, true);
        Require(Decide(l5, 1, 1) == Outcome::Hub && Decide(l1, 2, 1) == Outcome::Hub && Decide(ne, 1, 1) == Outcome::Hub, "a far spawn: hub");
        Require(Decide(l5, 1, 0) == Outcome::AtHub, "L5 row, spawn within 100 yd: at_hub (no legacy)");
        Require(Decide(l5, 0, 0) == Outcome::Fallback, "L5 row, no spawn at all: fallback (legacy)");
        Require(Decide(l1, 1, 0) == Outcome::Fallback && Decide(ne, 1, 0) == Outcome::Fallback,
            "L1-4 / next_zone row at the hub: today's fallback (unchanged)");
        Require(Decide(nullptr, 1, 0) == Outcome::Fallback, "no row: fallback");
        Require(std::strcmp(OutcomeName(Outcome::Hub), "hub") == 0 && std::strcmp(OutcomeName(Outcome::AtHub), "at_hub") == 0 &&
            std::strcmp(OutcomeName(Outcome::Fallback), "fallback") == 0, "outcome names (hub / fallback as in today's line)");
        Counters counters;
        counters.atHub.fetch_add(1);
        Require(counters.atHub.exchange(0) == 1 && counters.atHub.load() == 0, "at_hub counter takes and resets");
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
