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

// The tank tab of the class: warrior 2 (protection), paladin 1 (protection); other classes have none here.
inline bool IsTankTab(std::uint32_t playerClass, int tab)
{
    return (playerClass == kClassWarrior && tab == 2) || (playerClass == kClassPaladin && tab == 1);
}

// The DPS tab a too-early tank gets: warrior 0 (arms), paladin 2 (retribution); -1 for other classes.
inline int DpsTab(std::uint32_t playerClass)
{
    return playerClass == kClassWarrior ? 0 : playerClass == kClassPaladin ? 2 : -1;
}

// twow-repo#602 tank-kit path default (OB-00 29.10.2026, same switch): a warrior without talent points (below 10, or none spent yet)
// has the default tab 2 (Protection) from GetPlayerMajorityTab, so an arms or fury warrior reaching 10 was rebuilt to the tank kit
// (tank-B 28.10.: Wendell / Aelanthil arms, Kriakka fury). With the switch such a warrior starts from the tab of its premade path.
// The path's tab: its AiPlayerbot.PremadeSpecTab key, else the main tree of its first link (the live config has no keys for
// warriors); -1 = no usable tab.
inline int PathKitTab(int pathTab, int firstLinkTree)
{
    if (pathTab >= 0 && pathTab <= 2)
        return pathTab;
    return firstLinkTree >= 0 && firstLinkTree <= 2 ? firstLinkTree : -1;
}

// The tab the kit decision starts from: warriors only (a paladin already defaults to retribution; every other class keeps its
// engines), only without talent points and only with a path; else the spec tab as before.
inline int BaseTab(bool applies, std::uint32_t playerClass, int specTab, bool noTalentPoints, int pathKitTab)
{
    if (applies && playerClass == kClassWarrior && noTalentPoints && pathKitTab >= 0 && pathKitTab <= 2)
        return pathKitTab;
    return specTab;
}

// Strategies are not rebuilt on a level-up: a level change across the key level of the class (either way) needs one rebuild.
inline bool CrossesKey(std::uint32_t playerClass, std::uint32_t fromLevel, std::uint32_t toLevel)
{
    std::uint32_t const key = playerClass == kClassWarrior ? kWarriorTankLevel : playerClass == kClassPaladin ? kPaladinTankLevel : 0;
    return key && fromLevel && (fromLevel < key) != (toLevel < key);
}

// [TankKit] counters. Strategy (re)inits / level crossings add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> demoted{ 0 };   // combat-engine builds with the DPS kit instead of the tank kit (a rate, not bots)
inline std::atomic<std::uint64_t> rebuilt{ 0 };   // strategy rebuilds because a level change crossed the key level
inline std::atomic<std::uint64_t> pathed{ 0 };    // combat-engine builds of a warrior whose kit follows its path, not the default tab (a rate)
}
