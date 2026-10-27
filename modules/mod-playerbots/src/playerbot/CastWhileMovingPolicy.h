#pragma once

#include <cstdint>

namespace ai::cast_moving
{
// twow-repo#333 P1-w (OB-00 27.10.2026, M1r run 4: 0 gathering skill-ups). PlayerbotAI::CastSpell stops a moving bot before a
// spell with a cast time and then cancelled the cast unless a real player is the master - before SpellStart, so the action
// failed, the engine ran the next action in the same tick, that action moved the bot again, and the next try cancelled again
// (action log: CAST_FAIL reason=none state=null at 3-4 s intervals on one spot; Skinning 12 of 12, Opening ~156 of ~162 for
// one bot). A loot cast - OPEN_LOCK (mining, herbs, chests, quest objects) or SKINNING with its loot target found and no item
// target (Pick Lock on a lockbox is not one) - now casts after the stop. Every other cast keeps the old rule (combat casts:
// CLI-ROT). A started loot cast also holds the AI for its cast time (OpenLootAction): the engine replaced the cast wait with
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

// After StopMoving() for a cast-time or channelled spell (jumping and falling cancel before): cast with an active real player
// master (as before) or for a loot cast, otherwise cancel.
inline bool CancelAfterStop(bool activePlayerMaster, bool lootCast)
{
    return !activePlayerMaster && !lootCast;
}

// The duration of an OpenLootAction that succeeded: at least its own (react delay, or the loot delay of a corpse), and the
// cast time of a started loot cast, so the engine keeps the AI waiting until the cast is done.
inline std::uint32_t LootActionDuration(std::uint32_t actionDuration, std::uint32_t castDuration)
{
    return castDuration > actionDuration ? castDuration : actionDuration;
}
}
