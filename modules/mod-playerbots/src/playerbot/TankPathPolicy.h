#pragma once

#include <cstdint>
#include <string>

namespace ai::tank_path
{
// #357 / #367 (owner 2026-09-27, phase 1 bots only): a shaman or rogue tank is
// decided by its premade path, like the bear (#308, druid 11.3). The paths
// share the build of Enhancement (7.1) and Combat (4.0); the difference is
// role, strategy and stats. Both ship with roll weight 0 (off) until the owner
// signs off the design numbers.
constexpr std::uint8_t ClassRogue = 4;
constexpr std::uint8_t ClassShaman = 7;

constexpr char const* ShamanTankPath = "shaman tank";
constexpr char const* RogueTankPath = "rogue tank";

constexpr char const* ShamanTankStrategy = "tank shaman";
constexpr char const* RogueTankStrategy = "tank rogue";

// The class tank strategy of a premade path, or nullptr for a non-tank path
// (the bear keeps its own "tank feral" handling in AiFactory).
inline char const* StrategyFor(std::uint8_t cls, std::string const& pathName)
{
    if (cls == ClassShaman && pathName == ShamanTankPath)
        return ShamanTankStrategy;
    if (cls == ClassRogue && pathName == RogueTankPath)
        return RogueTankStrategy;
    return nullptr;
}

inline bool IsTankPath(std::uint8_t cls, std::string const& pathName)
{
    return StrategyFor(cls, pathName) != nullptr;
}

// Owner 2026-09-27 (#357): the shaman tank uses Stormstrike only while it holds
// aggro and with at least 4 Lightning Shield charges (Stormstrike consumes up
// to 3; the charge damage comes later as a bot aura).
constexpr std::uint32_t StormstrikeMinShieldCharges = 4;

inline bool AllowTankStormstrike(bool holdsAggro, std::uint32_t shieldCharges)
{
    return holdsAggro && shieldCharges >= StormstrikeMinShieldCharges;
}
}
