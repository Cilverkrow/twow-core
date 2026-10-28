#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::cast_moving
{
// twow-repo#333 P1-w (OB-00 27.10.2026, M1r run 4: 0 gathering skill-ups). PlayerbotAI::CastSpell stops a moving bot before a
// spell with a cast time and then cancelled the cast unless a real player is the master - before SpellStart, so the action
// failed, the engine ran the next action in the same tick, that action moved the bot again, and the next try cancelled again
// (action log: CAST_FAIL reason=none state=null at 3-4 s intervals on one spot; Skinning 12 of 12, Opening ~156 of ~162 for
// one bot). A loot cast - OPEN_LOCK (mining, herbs, chests, quest objects) or SKINNING with its loot target found and no item
// target (Pick Lock on a lockbox is not one) - now casts after the stop, always. Every other cast: Rotation.CastStopFirst
// below. A started loot cast also holds the AI for its cast time (OpenLootAction): the engine replaced the cast wait with
// the action's react delay, and the next movement action interrupted the cast (CastWait.Preparing is 0 by default).
// Pure rules, no game types.

// SpellEffects of SpellDefines.h.
constexpr std::uint32_t kSpellEffectOpenLock = 33;
constexpr std::uint32_t kSpellEffectSkinning = 95;

// A loot cast: the first effect opens a lock or skins, CastSpell found the loot target (a spawned game object or a unit), and
// the spell has no item target.
inline bool IsLootCast(std::uint32_t firstEffect, bool lootTargetFound, bool itemTarget)
{
    return lootTargetFound && !itemTarget && (firstEffect == kSpellEffectOpenLock || firstEffect == kSpellEffectSkinning);
}

// twow-repo#602 ROT-01 (AiPlayerbot.Rotation.CastStopFirst, OB-00 27.10.2026, code default 1, 0 = rollback): the same cancel hit
// every combat cast of a bot on its own. hunt2 action logs (24 sample bots, core ec58eb24): 47,115 CAST_FAIL reason=none
// state=null (Mind Blast 26.2k, Multi-Shot 9.1k, Arcane Missiles 3.0k, Fireball 3.0k, Frostbolt 2.0k, Smite 1.4k; loot ~1.5k),
// 87 % right after "reach spell" OK, 740 streaks of 10 or more without a CAST_OK (longest 141). With the switch every cast-time
// or channelled spell casts after the stop, in all three CastSpell overloads (unit, game object, position).
// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool CastStopFirstApplies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// After StopMoving() for a cast-time or channelled spell (jumping and falling cancel before): cast with an active real player
// master (as before), for a loot cast, or with Rotation.CastStopFirst; otherwise cancel.
inline bool CancelAfterStop(bool activePlayerMaster, bool lootCast, bool castStopFirst)
{
    return !activePlayerMaster && !lootCast && !castStopFirst;
}

// A cast that only Rotation.CastStopFirst lets through (the old rule cancelled it): counted for the [CastStopFirst] line.
inline bool CastByStopFirst(bool activePlayerMaster, bool lootCast, bool castStopFirst)
{
    return castStopFirst && CancelAfterStop(activePlayerMaster, lootCast, false);
}

// [CastStopFirst] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> castAfterStop{ 0 };   // casts that went on to SpellStart after the stop because of the switch
inline std::atomic<std::uint64_t> cancelled{ 0 };       // casts still cancelled after the stop (bots outside the switch's cohort)

// The duration of an OpenLootAction that succeeded: at least its own (react delay, or the loot delay of a corpse), and the
// cast time of a started loot cast, so the engine keeps the AI waiting until the cast is done.
inline std::uint32_t LootActionDuration(std::uint32_t actionDuration, std::uint32_t castDuration)
{
    return castDuration > actionDuration ? castDuration : actionDuration;
}
}
