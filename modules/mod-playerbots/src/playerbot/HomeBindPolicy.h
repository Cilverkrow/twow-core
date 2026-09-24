#pragma once

#include <cstdint>

namespace ai::homebind
{
// #307: 23 of 136 roster bots (level 1-11) had their hearthstone bound in
// Southshore (Hillsbrad, a level 20-30 zone). Every "long stuck" hearthstone
// then sent them back, and they re-bound at the same inn. A roster bot neither
// binds nor hearths to a zone clearly above its level. An unknown zone level
// (0, e.g. custom zones) is never treated as too high.
inline bool IsZoneClearlyAboveLevel(std::uint32_t areaLevel, std::uint32_t botLevel, std::uint32_t margin = 5)
{
    return areaLevel > 0 && areaLevel > botLevel + margin;
}
}
