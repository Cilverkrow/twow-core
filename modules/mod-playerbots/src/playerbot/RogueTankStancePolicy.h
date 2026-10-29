#pragma once

#include <cstdint>

#include "CohortPolicy.h"

namespace ai::rogue_tank_stance
{
// twow-repo#654 (WS10-ROGUE-TANK-STANCE-01, owner 29.10.2026 "Als Tank-Haltung", "Alles nur in Haltung", "Nur Stealth und Vanish";
// AiPlayerbot.Rotation.RogueTankStance, code default 0, BEHAVIOUR CHANGE, A/B with 2): Shadow Dance becomes a rogue tank stance in the
// core (Funserver.Rogue.ShadowDanceStance, FunserverShadowDanceStance.h): the stance 61230 "Shadow Dance" carries the penalties, and
// the passive ranks give their bonus only while it is up. With the switch a bot on the rogue tank path (4.3) learns the stance from
// level 20 when the core switch covers bots (1 or 2), keeps it up while it runs the "tank rogue" strategy, and drops it otherwise; it
// never tries Stealth or Vanish in the stance (the core refuses both there). Pure rules, no game types.

constexpr std::uint32_t StanceSpell = 61230;   // FunserverShadowDanceStance.h SHADOW_DANCE_STANCE
constexpr std::uint32_t MinLevel = 20;         // Shadow Dance rank 1

// The bot switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// The core switch covers bots: 1 = every rogue, 2 = bots only.
inline bool CoreCoversBots(std::uint32_t coreMode)
{
    return coreMode == 1 || coreMode == 2;
}

// The bot learns the stance: switch, core switch, the rogue tank path, level 20. Otherwise a known stance goes.
inline bool Teach(bool applies, bool coreCovers, bool tankPath, std::uint32_t level)
{
    return applies && coreCovers && tankPath && level >= MinLevel;
}

// The stance stays up only while the bot tanks ("tank rogue" strategy); otherwise it is cancelled.
inline bool WantsUp(bool applies, bool coreCovers, bool tankStrategy)
{
    return applies && coreCovers && tankStrategy;
}

inline bool Cancel(bool stanceUp, bool wantsUp)
{
    return stanceUp && !wantsUp;
}

// Stealth / Vanish are not useful in the stance (the core refuses them: no blocked-cast loop).
inline bool StealthUseful(bool stanceUp)
{
    return !stanceUp;
}
}
