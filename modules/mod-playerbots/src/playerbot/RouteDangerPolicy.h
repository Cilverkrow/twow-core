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

inline Reason Classify(bool crossMap, std::uint32_t botLevel, std::uint32_t minCrossMapLevel,
    std::uint32_t targetZoneLevel, std::uint32_t margin = 5)
{
    if (crossMap && minCrossMapLevel > 0 && botLevel < minCrossMapLevel)
        return Reason::CrossMap;

    if (targetZoneLevel > 0 && targetZoneLevel > botLevel + margin)
        return Reason::TargetZoneLevel;

    return Reason::None;
}
}
