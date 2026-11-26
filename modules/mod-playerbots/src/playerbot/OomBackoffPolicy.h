#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::oom_backoff
{
// twow-repo#602 ROT-01 3c-B (priest Farathil loop, OB-00 24.10.2026 "rumstehen ist keine Option"; AiPlayerbot.Rotation.OomBackoff,
// default 0, BEHAVIOUR-CHANGING): an out-of-mana caster without a wand stood in combat for hours. Every AI tick (~7/s) pushed
// its whole rotation - each spell PREREQ -> reach spell USELESS -> IMPOSSIBLE, "shoot" IMPOSSIBLE without a wand - executed
// nothing and started again (x1b 23.10.: 16:49-22:12Z, ~100 MB/h action log, ~21 ms of triggers per tick). Two parts:
// - melee fallback: a priest, mage or warlock below the "no mana" mark without a wand attacks in melee ("melee"), and
//   "enemy too close for spell" does not send it away from that melee;
// - backoff: a combat pass that executed nothing while the bot is below the mark waits DelayMs instead of ReactDelay; the
//   wait ends early once the mana is back at the mark or the health drops below LowHealth.
// Pure rules, no game types.

// Class ids of the 1.12 core (SharedDefines.h): the casters whose only ranged fallback is a wand.
constexpr std::uint32_t ClassPriest = 5;
constexpr std::uint32_t ClassMage = 8;
constexpr std::uint32_t ClassWarlock = 9;

// The "no mana" mark (NoManaTrigger: mana below 5 percent).
constexpr std::uint32_t NoManaPct = 5;

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

inline bool WandClass(std::uint32_t cls)
{
    return cls == ClassPriest || cls == ClassMage || cls == ClassWarlock;
}

inline bool OutOfMana(bool hasMana, std::uint32_t manaPct)
{
    return hasMana && manaPct < NoManaPct;
}

// twow-repo#602 ROT-01 I-1 (owner decision relayed by OB-00 25.11.2026 "Nahkampf ja"; AiPlayerbot.Rotation.PriestOomMelee,
// default 0, BEHAVIOUR-CHANGING): the melee fallback alone, for priests only - no backoff wait, mage and warlock unchanged.
// Mode: 0 off, 1 every bot, 2 cohort B only, 3 cohort A only (CohortPolicy.h opt-in, for a crossover: unit 2 swaps the arm).
inline bool PriestMeleeApplies(std::uint32_t priestMode, std::uint32_t cls, std::uint32_t guidLow)
{
    return cls == ClassPriest && Applies(priestMode, guidLow);
}

// The melee fallback is armed for this bot by either switch (OomBackoff: priest / mage / warlock; PriestOomMelee: priests).
inline bool MeleeArmed(std::uint32_t oomMode, std::uint32_t priestMode, std::uint32_t cls, std::uint32_t guidLow)
{
    return Applies(oomMode, guidLow) || PriestMeleeApplies(priestMode, cls, guidLow);
}

// Counter attribution: the [PriestOomMelee] counters take a bot only when OomBackoff does not apply to it.
inline bool ViaPriestKey(std::uint32_t oomMode, std::uint32_t priestMode, std::uint32_t cls, std::uint32_t guidLow)
{
    return !Applies(oomMode, guidLow) && PriestMeleeApplies(priestMode, cls, guidLow);
}

// Melee fallback: the switch applies, a wand class, out of mana, no wand equipped.
inline bool MeleeFallback(bool applies, std::uint32_t cls, bool hasMana, std::uint32_t manaPct, bool hasWand)
{
    return applies && WandClass(cls) && OutOfMana(hasMana, manaPct) && !hasWand;
}

// Backoff after a combat pass: the switch applies, the combat engine executed nothing, YieldAIInternalThread set the delay,
// not a minimal pass, not casting, out of mana and not below LowHealth (a bot in danger keeps reacting every pass).
inline bool MayBackoff(bool applies, bool combatIdleTick, bool yieldSetDelay, bool minimal, bool casting, bool hasMana,
    std::uint32_t manaPct, std::uint32_t healthPct, std::uint32_t lowHealthPct)
{
    return applies && combatIdleTick && yieldSetDelay && !minimal && !casting && OutOfMana(hasMana, manaPct) &&
        healthPct >= lowHealthPct;
}

// A pending backoff ends early when the mana is back at the mark or the health dropped below LowHealth.
inline bool ShouldWake(bool pending, bool hasMana, std::uint32_t manaPct, std::uint32_t healthPct, std::uint32_t lowHealthPct)
{
    return pending && (!OutOfMana(hasMana, manaPct) || healthPct < lowHealthPct);
}

// The wake cuts a pending delay of at most the backoff itself; a longer delay someone else set (a teleport) is kept.
inline bool WakeResets(std::uint32_t currentDelay, std::uint32_t backoffDelay)
{
    return currentDelay != 0 && currentDelay <= backoffDelay;
}

// [OomBackoff] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> fallback{ 0 };   // "oom melee fallback" evaluations that fired
inline std::atomic<std::uint64_t> stayed{ 0 };     // "enemy too close for spell" evaluations kept quiet for such a bot
inline std::atomic<std::uint64_t> backoffs{ 0 };   // combat passes followed by the backoff
inline std::atomic<std::uint64_t> woken{ 0 };      // backoffs ended early (mana back, health low)

// [PriestOomMelee] counters (I-1): the same two events for bots armed only by PriestOomMelee (ViaPriestKey), per cohort
// (index 0 = A even low guid, 1 = B odd), so the line itself shows the control null of the OFF cohort (OB-10 A7: no [ACTION] lines).
inline std::atomic<std::uint64_t> priestFallback[2]{ { 0 }, { 0 } };   // "oom melee fallback" evaluations that fired
inline std::atomic<std::uint64_t> priestStayed[2]{ { 0 }, { 0 } };     // "enemy too close for spell" evaluations kept quiet for such a bot

// Counter index of a bot's cohort: 0 = A, 1 = B (CohortPolicy.h InB).
inline std::uint32_t CohortIndex(std::uint32_t guidLow)
{
    return ai::cohort::InB(guidLow) ? 1u : 0u;
}
}
