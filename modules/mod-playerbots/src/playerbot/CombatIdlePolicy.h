#pragma once

#include <atomic>
#include <cstdint>

namespace ai::combat_idle
{
// twow-repo#541 (audit A17, AiPlayerbot.Perf.CombatIdleYield, default 0, BEHAVIOUR-CHANGING):
// today a combat AI pass that executes no action ("no actions executed") is followed by the next full
// pass after ReactDelay, which in practice is the next map update, because CanUpdateAIInternal is delay < 100.
// A bot that only auto-attacks while it waits for energy, rage, mana or a cooldown therefore reruns every
// trigger and every pushed action on every map update. With the switch on, that wait is
// ReactFactor x ReactDelay. It ends early when the auto attack it waited on changes.
constexpr std::uint32_t ReactFactor = 3;

inline std::uint32_t StretchedDelay(std::uint32_t reactDelay)
{
    return reactDelay * ReactFactor;
}

// Cheap per-pass preconditions (all are locals of UpdateAI): switch on, the combat engine executed nothing,
// YieldAIInternalThread was the one that set the delay, and not a minimal (out-of-combat) yield.
inline bool MayStretch(bool switchOn, bool combatIdleTick, bool yieldSetDelay, bool minimalYield)
{
    return switchOn && combatIdleTick && yieldSetDelay && !minimalYield;
}

// Auto attack: the victim is the current target, and a melee swing (victim in melee reach) or an autorepeat
// spell (auto shot or wand) is running.
inline bool IsAutoAttacking(bool victimIsCurrentTarget, bool meleeSwing, bool autoRepeat)
{
    return victimIsCurrentTarget && (meleeSwing || autoRepeat);
}

// A pending stretched wait ends when the victim changed or disappeared, or the auto attack stopped
// (melee swing: also when the victim left melee reach).
inline bool ShouldWake(std::uint64_t pendingVictim, std::uint64_t victim, bool meleeSwing, bool autoRepeat)
{
    return pendingVictim != 0 && (victim != pendingVictim || !(meleeSwing || autoRepeat));
}

// The wake cuts a pending delay of at most the stretched wait (normally the stretch itself). A longer delay
// that someone else set meanwhile, such as a teleport, is kept.
inline bool WakeResets(std::uint32_t currentDelay, std::uint32_t stretchedDelay)
{
    return currentDelay != 0 && currentDelay <= stretchedDelay;
}

// [CombatIdle] counters. Region threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> stretched{ 0 };
inline std::atomic<std::uint64_t> woken{ 0 };
}
