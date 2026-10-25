#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::hunter_melee_start
{
// twow-repo#602 ROT-01 hunter deaths (focus check 25.10.2026, OB-00; AiPlayerbot.Rotation.HunterMeleeStart, default 0,
// BEHAVIOUR-CHANGING): hunters die 2.05 times per bot-hour against 0.60-1.28 for the other classes (X3a, 540 bots), mostly alone
// against one weaker mob. One cause: the hunter's "switch to melee" calls Player::MeleeAttackStart, which is an empty stub in this
// core (Player.h, "cmangos forwarders ... Stub no-op"), and AttackAction starts melee only for a non-ranged bot or a target under
// 5 yd - IsRanged() is still true for a hunter in "close" mode. So a hunter that went to melee from 5 yd or more never swung:
// Raptor Strike stayed queued (IMPOSSIBLE), auto shot is off in close mode, and the bot stood until it died (11 of 32 sampled
// hunter deaths on track C, 24.10.). With the switch the two hunter actions really start/stop the melee auto attack
// (Unit::Attack with meleeAttack / clearing UNIT_STAT_MELEE_ATTACKING), and AttackAction starts melee for a hunter in "close".
// Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// AttackAction's melee flag: the old rule (not ranged, or the target within 5 yd), plus a hunter in "close" mode when the switch
// applies.
inline bool AttackWithMelee(bool ranged, float distance2d, bool applies, bool closeMode)
{
    return !ranged || distance2d < 5.0f || (applies && closeMode);
}

// [HunterMelee] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> started{ 0 };   // "switch to melee" that switched the melee auto attack on
inline std::atomic<std::uint64_t> stopped{ 0 };   // "switch to ranged" that switched it off
inline std::atomic<std::uint64_t> closeAttacks{ 0 };   // attacks in close mode that start melee only because of the switch
}
