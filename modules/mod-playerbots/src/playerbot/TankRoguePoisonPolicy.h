#pragma once

#include <cstdint>

#include "CohortPolicy.h"

namespace ai::tank_rogue_poison
{
// twow-repo#654 step 1 (OB-00 / owner 29.10.2026: "fix the poison ping-pong bug first"; AiPlayerbot.Rotation.TankRoguePoison, code
// default 1, 0 = rollback): a rogue tank (#367, premade path 4.3) coats its main hand with the threat poison (Agitating Poison: bot item
// 90140 / 65032), while the combat kit under it keeps its own main-hand poison (Instant Poison). Each "apply ... poison main hand"
// trigger sees the other poison as missing, so the two overwrote each other out of combat (soak: bot 3168 got the kit item 18 times in
// 10 h). TankRogueThreatMultiplier was meant to keep every other main-hand poison off, but it matched the suffix " poison main hand" on
// the action's name - the actions are named "apply instant poison" etc. (the hand lives in the action, not in its name), so the check
// never matched, and it ran in the combat engine only. With the switch the multiplier tells the poisons by the action itself (hand +
// threat poison) and runs out of combat as well. Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// A poison action for the main hand that is not the threat poison: the tank's multiplier makes it useless (0).
inline bool BlocksMainHand(bool isPoisonAction, bool mainHand, bool isThreatPoison)
{
    return isPoisonAction && mainHand && !isThreatPoison;
}
}
