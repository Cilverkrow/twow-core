#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::melee_flee_break
{
// twow-repo#602 ROT-01 (death analysis 21.10.2026, OB-10/OB-00; AiPlayerbot.Rotation.MeleeFleeBreak, default 0,
// BEHAVIOUR-CHANGING): below MediumHealth "outnumbered" pushes "flee" (ACTION_EMERGENCY + 9) as soon as the mobs fighting
// the bot outweigh it. FleeMultiplier stops a flee loop only for ranged bots (two flee dispatches within ReturnDelay drop
// flee to 0.01, and the nukes win). A melee bot cannot outrun the mobs it flees from, so it fled on every AI tick, never
// swung, and leech healed nothing (L1-b: 7 of 7 melee deaths right after a flee episode; C ladder 21.10.: 173 of 186
// melee flee episodes ended in death within 90 s, against 3-19 % per ranged bot). A 0.01 relevance is not enough for
// melee: with no ability usable and melee already swinging ("melee - FAILED"), the queue runs dry and the damped flee
// still runs. So with the switch on, "outnumbered" itself stays quiet for a melee bot while its flee loop holds - after
// two flee dispatches within the window the bot fights until the window has passed, then one new attempt is allowed.
// Panic (critical health, X3 EmergencyLadder) is not touched. Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h); ranged bots keep FleeMultiplier's own break.
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow, bool ranged)
{
    return !ranged && ai::cohort::Applies(mode, guidLow);
}

// The same loop test as FleeMultiplier (FleeStrategy.cpp): the last flee dispatch lies within the window (seconds,
// ReturnDelay / 1000) and it was at least the second one in a row.
inline bool Holds(std::int64_t now, std::int64_t lastFleeAttempt, std::uint32_t fleeCount, std::int64_t windowSeconds)
{
    return lastFleeAttempt != 0 && now - lastFleeAttempt <= windowSeconds && fleeCount >= 2;
}

// [MeleeFleeBreak] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> held{ 0 };      // trigger evaluations that would have fired but were held
inline std::atomic<std::uint64_t> breaks{ 0 };    // hold windows started (first held evaluation after a fired or quiet one)
inline std::atomic<std::uint64_t> allowed{ 0 };   // evaluations that fired for a melee bot the switch applies to
}
