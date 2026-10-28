#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::invalid_target
{
// twow-repo#602 ROT-01 failure census (OB-00 28.10.2026; AiPlayerbot.Rotation.InvalidTargetQuiet, code default 1, 0 = rollback): the combat
// trigger "invalid target" (ClassStrategy, emergency) reads InvalidTargetValue, which is also true for an EMPTY current target. After a
// kill, while the combat engine runs and nothing attacks the bot, it fired on every tick; "select new target" then had nothing to clear
// and nobody to pick (it only re-picks through dps / tank assist when "has attackers"), returned false, and the AI re-ticked every
// 20-40 ms (hunt2: 90,889 FAILED, 186 streaks of 50 or more, Zexnik 1,274 in a row in 6 min). With the switch an empty target fires
// only while the bot has attackers - that is the combat retarget to the next add and stays; a non-empty invalid target fires as before.
// Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// The trigger stays quiet: switch on, no current target, and nothing to retarget to.
inline bool Quiet(bool applies, bool targetEmpty, bool hasAttackers)
{
    return applies && targetEmpty && !hasAttackers;
}

// [InvalidTarget] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> quiet{ 0 };            // trigger checks kept quiet by the switch
// Trace for the follow-up (b) (log only, no behaviour): "select new target" dropped a LIVE current target, and whether that interrupted a
// running cast (InterruptSpell + AttackStop also hit a live but "invalid" target - selection mismatch, a mob without a target of its own).
inline std::atomic<std::uint64_t> droppedLive{ 0 };      // the dropped current target was alive
inline std::atomic<std::uint64_t> interruptedLive{ 0 };  // ... and the bot was casting or channelling (the drop interrupted it)
}
