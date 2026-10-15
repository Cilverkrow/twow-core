#pragma once

// twow-repo#581 QL-3 (owner 14.10.2026 via OB-00): a quest target with only a few spawns (Annoying Peasant 50003,
// Defias Messenger 550, ...) is a bottleneck when many players and bots need it - they wait out its whole respawn
// one after the other. With DynamicRespawn.QuestTarget.Enable (default 0) such a creature respawns faster the more
// players nearby still need it for a quest: delay / (1 + PerPlayer * interested), never below MinSeconds and never
// longer than before. Pure rules, no server types (t/quest_target_respawn_policy_test.cpp).

#include <algorithm>
#include <cstdint>

namespace quest_target_respawn
{
    // A creature entry qualifies when it is a quest target (kill objective or quest drop) and has at most maxSpawns
    // spawns in the world.
    inline bool IsCandidate(std::uint32_t spawns, std::uint32_t maxSpawns, bool questTarget)
    {
        return questTarget && spawns > 0 && spawns <= maxSpawns;
    }

    // The respawn delay in seconds for `interested` players (with the quest, in range). 0 interested = unchanged.
    inline std::uint32_t ScaledDelay(std::uint32_t delay, std::uint32_t interested, float perPlayer, std::uint32_t minSeconds)
    {
        if (!interested || perPlayer <= 0.0f || delay <= minSeconds)
            return delay;
        float const scaled = float(delay) / (1.0f + perPlayer * float(interested));
        std::uint32_t const result = std::max(minSeconds, std::uint32_t(scaled));
        return std::min(result, delay);
    }
}
