#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::dark_rune_gate
{
// twow-repo#602 ROT-01 3c (priest Farathil loop, OB-00 24.10.2026; AiPlayerbot.Rotation.DarkRuneGate, default 0,
// BEHAVIOUR-CHANGING): UsePotionsStrategy pushes "dark rune" on every "low mana" evaluation, combat and reaction engine
// alike. Without a Dark Rune in the bags the action is impossible and dropped, so an out-of-mana bot pushed it 2-3 times
// per AI tick for hours (x1b 23.10.: 16:49-22:12Z, ~100 MB/h action log). With the switch the strategy asks the trigger
// "low mana consumable" instead: low mana AND "dark rune" or its node alternative "mana potion" is possible and useful
// (item in the bags or the item cheat without #485 UseReal, no item cooldown, for the rune its required level, not a mage,
// not in an arena; for the potion #485 ManaPotionPct). The potion has to count: as the rune's alternative it is the only way
// most classes drink a mana potion in combat. A failed check is not repeated for RecheckSeconds. Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// A check is due once the wait after the last failed check has passed (nextCheck 0 = never failed).
inline bool Due(std::int64_t now, std::int64_t nextCheck)
{
    return now >= nextCheck;
}

// The time of the next check after a failed one.
inline std::int64_t NextCheck(std::int64_t now, std::uint32_t recheckSeconds)
{
    return now + std::int64_t(recheckSeconds);
}

// The item's required level (the old action never checked it: under the item cheat a bot below it would use the item).
// The Dark Rune itself has required_level 0 in tw_world today, so this only guards a data change.
inline bool LevelOk(std::uint32_t botLevel, std::uint32_t requiredLevel)
{
    return botLevel >= requiredLevel;
}

// [DarkRuneGate] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> checks{ 0 };    // low mana evaluations that checked the actions
inline std::atomic<std::uint64_t> passed{ 0 };    // ... and found the rune or the potion usable (the trigger fired)
inline std::atomic<std::uint64_t> blocked{ 0 };   // ... and found neither usable (the wait starts)
inline std::atomic<std::uint64_t> waited{ 0 };    // evaluations skipped inside the wait
}
