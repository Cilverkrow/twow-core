#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::cast_los_gate
{
// twow-repo#602 ROT-01 hunter LoS (focus check 25.10.2026, OB-00; AiPlayerbot.Rotation.CastLosGate, default 0, BEHAVIOUR-CHANGING):
// sampled hunters failed 91-96 % of their Auto Shot / Hunter's Mark / Serpent Sting casts with SPELL_FAILED_LINE_OF_SIGHT, at
// about 19 core rejections per second for minutes, while wand Shoot (which has its own LoS pre-filter) failed 0 %. The bot knew:
// CanCastSpell runs the real CheckCast and gets LINE_OF_SIGHT, but with ignoreRange (always set by CastSpellAction::isPossible)
// it counts that as castable, expecting the "reach" prerequisite to fix sight. The reach cannot: the chase distance stays 75 % of
// the range (Rotation.ReachLosClose fixes that), Auto Shot's Execute stops movement before every attempt, and after 20 s hotfix
// 8.2 marks the target unreachable - from then on reach is USELESS, yet the bot keeps the target and target selection picks it
// again (only GrindTargetValue skips unreachable targets). With the switch:
// - a LINE_OF_SIGHT verdict of the bot's own CheckCast makes the cast IMPOSSIBLE (the core would reject it anyway; spells with
//   the IGNORE_LOS attribute never get that verdict), so no retry loop and no StopMoving from the cast;
// - a current target that 8.2 marked unreachable is dropped at such a verdict, and target selection skips unreachable units
//   while another candidate exists.
// Meant together with Rotation.ReachLosClose = 2 (the walk into sight). Closes OB-10's anti-stall gap L2. Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// Target selection skips an unreachable unit only while another candidate exists (a bot with nothing else left keeps defending).
inline bool SkipUnreachable(bool applies, bool unreachable, bool otherCandidate)
{
    return applies && unreachable && otherCandidate;
}

// [CastLosGate] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> gated{ 0 };     // casts made IMPOSSIBLE by a LINE_OF_SIGHT verdict
inline std::atomic<std::uint64_t> dropped{ 0 };   // current targets dropped because 8.2 marked them unreachable
inline std::atomic<std::uint64_t> skipped{ 0 };   // unreachable units skipped by target selection
}
