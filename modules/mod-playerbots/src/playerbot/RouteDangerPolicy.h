#pragma once

#include <cstdint>

namespace ai::route_danger
{
// #307: after the level-1 reset, 81 deaths in two hours were level 2-4 bots
// walking a cross-continent turn-in route (Durotar -> Undercity). The per-bot
// death rule only reacts after each bot has died twice, so every bot paid for
// the same route. A quest target is deferred up front when its route is a
// continent switch below a minimum level, or when its zone is clearly above
// the bot's level. An unknown zone level (0) is never treated as too high.
enum class Reason : std::uint8_t
{
    None,
    CrossMap,
    TargetZoneLevel,
};

// twow-repo#485: a switch between the two continents (maps 0 and 1, as
// MapEntry::IsContinent in the core). Both ends go through ContinentOf first.
inline bool IsContinentSwitch(std::uint32_t fromMapId, std::uint32_t toMapId)
{
    bool const fromContinent = fromMapId == 0 || fromMapId == 1;
    bool const toContinent = toMapId == 0 || toMapId == 1;
    return fromContinent && toContinent && fromMapId != toMapId;
}

// twow-repo#485: the continent a route really starts or ends on. 0 and 1 are
// themselves. The Deeprun Tram (369, map_template ghost_entrance_map -1) runs
// between Stormwind and Ironforge, so it is the Eastern Kingdoms (0). An
// instance counts as the continent of its ghost entrance (Ragefire Chasm 389
// -> 1, the Deadmines 36 -> 0). Any other map (battleground, instance without
// a ghost entrance) stays its own map and is never a continent switch.
inline std::uint32_t ContinentOf(std::uint32_t mapId, std::int32_t ghostEntranceMapId)
{
    if (mapId == 0 || mapId == 1)
        return mapId;
    if (mapId == 369)
        return 0;
    if (ghostEntranceMapId == 0 || ghostEntranceMapId == 1)
        return std::uint32_t(ghostEntranceMapId);
    return mapId;
}

// crossMap: the target is on another map than the bot. With continentsOnly
// (twow-repo#485) only a continentSwitch is deferred by minCrossMapLevel;
// without it every other map is (old behaviour).
inline Reason Classify(bool crossMap, std::uint32_t botLevel, std::uint32_t minCrossMapLevel,
    std::uint32_t targetZoneLevel, std::uint32_t margin = 5, bool continentsOnly = false,
    bool continentSwitch = false)
{
    if (crossMap && (!continentsOnly || continentSwitch) && minCrossMapLevel > 0 && botLevel < minCrossMapLevel)
        return Reason::CrossMap;

    if (targetZoneLevel > 0 && targetZoneLevel > botLevel + margin)
        return Reason::TargetZoneLevel;

    return Reason::None;
}
}
