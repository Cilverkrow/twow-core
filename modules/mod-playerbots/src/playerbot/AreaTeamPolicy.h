#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

// twow-repo#575 (PATH-01 wave 2, owner 14.10.2026: "stormbreaker point ist tatsächlich ein horde camp"): areas whose
// AreaTable team is wrong for this realm get a configured team (AiPlayerbot.AreaTeamOverrides, empty = off). Read by
// WorldPosition::isEnemyHomeZoneFor, so travel targets, random teleports, the rescue and the repop home checks treat
// such an area as an enemy home zone. Example: 5576:horde marks Stormbreaker Point (Balor, #501 trap) hostile for the
// Alliance. Values as in AreaTable: 2 = alliance, 4 = horde.
namespace ai::area_team
{
constexpr std::uint32_t None = 0;
constexpr std::uint32_t Alliance = 2;
constexpr std::uint32_t Horde = 4;

// "horde" / "alliance" / "none" or the AreaTable number; anything else = invalid (-1).
inline int ParseTeam(std::string value)
{
    for (char& c : value)
        c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (value == "horde" || value == "4")
        return int(Horde);
    if (value == "alliance" || value == "2")
        return int(Alliance);
    if (value == "none" || value == "0")
        return int(None);
    return -1;
}

// The team of an area: the configured one if the id is listed, else the AreaTable team.
inline std::uint32_t TeamOf(std::map<std::uint32_t, std::uint32_t> const& overrides, std::uint32_t areaId, std::uint32_t tableTeam)
{
    if (overrides.empty())
        return tableTeam;
    auto const found = overrides.find(areaId);
    return found != overrides.end() ? found->second : tableTeam;
}
}
