// twow-repo#575 F9 / F-b V2: level-limited graveyards from custom_graveyards (src/game/CustomGraveyardPolicy.h).
#include "../src/game/CustomGraveyardPolicy.h"

#include <iostream>

namespace
{
    int failures = 0;

    void Require(bool ok, char const* label)
    {
        if (!ok)
        {
            std::cerr << "FAILED: " << label << "\n";
            ++failures;
        }
    }
}

int main()
{
    using namespace custom_graveyard;

    // Blackstone Island: map 1, zone 5536, row area 5536 (= whole zone), max_level 10.
    Require(Applies(1, 5536, 5536, 10, 1, 5536, 5538, 10), "L10 dying anywhere in zone 5536 gets the plaza graveyard");
    Require(Applies(1, 5536, 5536, 10, 1, 5536, 5536, 1), "L1 gets it too");
    Require(!Applies(1, 5536, 5536, 10, 1, 5536, 5538, 11), "from L11 the zone's normal graveyard (149) stays");
    Require(!Applies(1, 5536, 5536, 10, 1, 5536, 5538, 0), "level 0 = caller passes no level: never a custom graveyard");
    Require(!Applies(1, 5536, 5536, 10, 1, 14, 362, 5), "another zone (Durotar) is not affected");
    Require(!Applies(1, 5536, 5536, 10, 0, 5536, 5538, 5), "another map is not affected");

    // Area rule: 0 or area == zone = whole zone (Turtle rows, e.g. Venture Camp 406/406); else only that area.
    Require(AreaFits(5536, 0, 5539), "area 0 = whole zone");
    Require(AreaFits(406, 406, 4011), "area == zone = whole zone");
    Require(AreaFits(406, 4011, 4011), "a sub area matches itself");
    Require(!AreaFits(406, 4011, 406), "a sub area does not match the rest of the zone");

    // Team: each row gives one entry per team; 0 (GM .neargrave) takes either.
    Require(TeamFits(true, TeamHorde) && !TeamFits(true, TeamAlliance), "the horde entry is for the horde only");
    Require(TeamFits(false, TeamAlliance) && !TeamFits(false, TeamHorde), "the alliance entry is for the alliance only");
    Require(TeamFits(true, 0) && TeamFits(false, 0), "team 0 takes either entry");

    // Synthetic ids: far above the DBC (1..174), distinct per row and team.
    Require(SyntheticId(51, false) == 100102 && SyntheticId(51, true) == 100103, "row 51 -> 100102 / 100103");
    Require(SyntheticId(1, false) > 174 && SyntheticId(50, true) != SyntheticId(51, false), "ids stay out of the DBC range and unique");

    // Allowlist parsing (Graveyard.Custom.Zones).
    Require(ParseZones("5536") == std::set<uint32_t>{5536}, "one zone");
    Require(ParseZones("5536, 406") == (std::set<uint32_t>{406, 5536}), "a list");
    Require(ParseZones("").empty() && ParseZones("0").empty() && ParseZones(" , ").empty(), "empty or 0 = no zone");

    if (failures)
        return 1;
    std::cout << "custom_graveyard_policy_test: ok\n";
    return 0;
}
