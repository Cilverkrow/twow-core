#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::tank_kit
{
// twow-repo#602 ROT-01 tank count (OB-00 28.10.2026; AiPlayerbot.Rotation.TankKitFromLevel, code default 0, BEHAVIOUR CHANGE, A/B with 2):
// the tank kit ("protection", "tank assist", "pull", "pull back", "close") only from the level of the tank spells. Below level 10 (no
// talent points) AiFactory::GetPlayerMajorityTab gives every WARRIOR the default tab 2 = Protection, so every warrior L1-9 pulled as a
// tank whatever its path (live540: 47 of 90 warriors; M1r run 4: arms warrior Skeeltz L5 tried Defensive Stance / Shield Block / Sunder
// thousands of times as IMPOSSIBLE), and a protection paladin below Righteous Fury (L16) tanked without its threat aura. With the switch
// those bots get their DPS kit until the key level (warrior: arms; paladin: retribution). Druids below 10 already have the "leveling"
// kit (no forms yet), so they are not touched. S7, ad-hoc and real player groups pick no roles, so this applies to all of them.
// Pure rules, no game types.

constexpr std::uint32_t kClassWarrior = 1;
constexpr std::uint32_t kClassPaladin = 2;
constexpr std::uint32_t kWarriorTankLevel = 10;   // Defensive Stance, Taunt, Sunder Armor
constexpr std::uint32_t kPaladinTankLevel = 16;   // Righteous Fury

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// A bot on the tank tab is below the level of its tank spells: it gets the DPS kit instead (only with the switch).
inline bool TooEarly(bool applies, std::uint32_t playerClass, std::uint32_t level)
{
    if (!applies)
        return false;
    if (playerClass == kClassWarrior)
        return level < kWarriorTankLevel;
    if (playerClass == kClassPaladin)
        return level < kPaladinTankLevel;
    return false;
}

// [TankKit] counter. Strategy (re)inits add, the world thread takes it once per minute.
inline std::atomic<std::uint64_t> demoted{ 0 };   // combat engines that got the DPS kit instead of the tank kit
}
