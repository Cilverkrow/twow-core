#pragma once

#include <cstdint>

namespace ai::quest_area_level
{
// #335: quest givers and objectives used the grind gate ("area level must not
// exceed bot level - 2 - death count", minimum 1). A level 1 bot that had died
// once was refused every point around Dolanaar (area level 5): no quest route,
// only fights with the level 5 owls around it, more deaths, a lower effective
// level. For quest destinations the tolerance is the same "clearly above" rule
// as the route danger (bot level + margin); an unknown area level (<= 0) is
// not a reason to refuse. A negative margin keeps the old grind gate.
inline bool IsQuestLocationLevelValid(std::int32_t areaLevel, std::uint32_t botLevel, std::int32_t margin)
{
    if (areaLevel <= 0)
        return true;

    return areaLevel <= static_cast<std::int32_t>(botLevel) + margin;
}
}
