#pragma once

#include <algorithm>
#include <cstdint>

namespace ai::stuck_combat
{
// Hotfix 8.12 (OB-00 03.10.2026, bot 27 Nilenata): a priest stood more than 24 hours in
// combat with something it could neither kill nor shake off (combat=1, flee OK, no XP, no
// loot, no death), and the quest rescue never came because it requires !IsInCombat().
// A roster bot on its own that is in combat without any progress is stopped after 10
// minutes; if it is still stuck after 20, the rescue may act although it is in combat.
constexpr uint32_t StopSeconds = 10 * 60;
constexpr uint32_t RescueSeconds = 20 * 60;
// Hotfix 8.14 (v27: Relavanna, Eloreni): the stop alone did not last - a bot that cannot move
// is pulled back by the next attacker, and the quest rescue never runs in combat. A bot that is
// still within HomeMoveTolerance of its stop position at the rescue step goes home (homebind),
// at most once per HomeCooldownSeconds.
constexpr uint32_t HomeCooldownSeconds = 60 * 60;
constexpr float HomeMoveTolerance = 5.0f;

enum class Step : uint8_t
{
    None,
    Stop,       // CombatStop and clear the threat once
    Rescue      // allow the quest rescue in combat (every check while stuck)
};

struct State
{
    uint32_t since = 0;         // start of the current combat
    bool stopped = false;
    bool rescueLogged = false;
    bool stopSet = false;       // position at the stop step
    uint32_t stopMap = 0;
    float stopX = 0.0f;
    float stopY = 0.0f;
    uint32_t lastHome = 0;      // survives leaving combat

    // idleSeconds: time without progress (level, XP, quest state, declared skill-ups).
    Step Observe(bool inCombat, uint32_t idleSeconds, uint32_t now)
    {
        if (!inCombat)
        {
            since = 0;
            stopped = false;
            rescueLogged = false;
            stopSet = false;
            return Step::None;
        }
        if (!since)
            since = now;

        uint32_t const stuck = std::min(now - since, idleSeconds);
        if (!stopped && stuck >= StopSeconds)
        {
            stopped = true;
            return Step::Stop;
        }
        return stuck >= RescueSeconds ? Step::Rescue : Step::None;
    }

    void RememberStop(uint32_t map, float x, float y)
    {
        stopSet = true;
        stopMap = map;
        stopX = x;
        stopY = y;
    }

    // At the rescue step: true (once per HomeCooldownSeconds) when the bot did not get away from
    // where it was stopped.
    bool HomeDue(uint32_t map, float x, float y, uint32_t now)
    {
        if (!stopSet || map != stopMap)
            return false;
        float const dx = x - stopX;
        float const dy = y - stopY;
        if (dx * dx + dy * dy >= HomeMoveTolerance * HomeMoveTolerance)
            return false;
        if (lastHome && now - lastHome < HomeCooldownSeconds)
            return false;
        lastHome = now;
        return true;
    }

    uint32_t Minutes(uint32_t now) const { return since && now > since ? (now - since) / 60 : 0; }
};
}
