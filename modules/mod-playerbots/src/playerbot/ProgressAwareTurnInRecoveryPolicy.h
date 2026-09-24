#pragma once

#include <cstdint>

namespace ai::turnin_recovery
{
enum class RecoveryAction : std::uint8_t
{
    None,
    RecomputeRoute,
    SuppressRouteAndCooldown
};

// Value-only state deliberately keeps the recovery decision independent of
// WorldObject lifetime. Production provides a fresh observation whenever a
// travel target is checked; the policy neither moves nor teleports a bot.
struct Observation
{
    std::uint32_t now = 0;
    std::uint32_t targetEntry = 0;
    std::uint32_t questId = 0;
    std::uint32_t mapId = 0;
    std::uint32_t zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
    bool paused = false;
};

struct State
{
    bool initialized = false;
    std::uint32_t targetEntry = 0;
    std::uint32_t questId = 0;
    std::uint32_t mapId = 0;
    std::uint32_t zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float distance = 0.0f;
    std::uint32_t lastProgressAt = 0;
    std::uint8_t recoveryStage = 0;
    std::uint32_t suppressedEntry = 0;
    std::uint32_t suppressedMapId = 0;
    std::uint32_t suppressUntil = 0;
    // Deaths on the way to one turn-in. Kept apart from the progress fields:
    // the graveyard teleport looks like progress and a revived bot re-selects
    // the same target, so ResetProgress must not clear it (#307).
    std::uint32_t deathEntry = 0;
    std::uint32_t deathQuestId = 0;
    std::uint8_t deathsOnRoute = 0;

    void ResetProgress()
    {
        initialized = false;
        recoveryStage = 0;
    }
};

// A death on the way to a completed-quest turn-in is a failed attempt, not a
// pause. Without this a roster bot whose turn-in route crosses higher-level
// mobs died on it indefinitely (#307: 5 bots, 1,321 deaths in 21 hours).
// Returns true when the route is now suppressed for cooldownMs.
inline bool RecordDeathOnRoute(State& state, std::uint32_t targetEntry, std::uint32_t questId,
    std::uint32_t mapId, std::uint32_t now, std::uint32_t maxDeaths, std::uint32_t cooldownMs)
{
    if (state.deathEntry != targetEntry || state.deathQuestId != questId)
    {
        state.deathEntry = targetEntry;
        state.deathQuestId = questId;
        state.deathsOnRoute = 0;
    }

    if (state.deathsOnRoute < 255)
        ++state.deathsOnRoute;

    if (maxDeaths == 0 || state.deathsOnRoute < maxDeaths)
        return false;

    state.deathsOnRoute = 0;
    state.suppressedEntry = targetEntry;
    state.suppressedMapId = mapId;
    state.suppressUntil = now + cooldownMs;
    return true;
}

inline float SquaredDistance(float leftX, float leftY, float rightX, float rightY)
{
    float const dx = leftX - rightX;
    float const dy = leftY - rightY;
    return dx * dx + dy * dy;
}

inline RecoveryAction Observe(State& state, Observation const& current, std::uint32_t stallSeconds,
    std::uint32_t cooldownSeconds, float minimumProgressDistance = 25.0f)
{
    bool const targetChanged = !state.initialized || state.targetEntry != current.targetEntry ||
        state.questId != current.questId;
    if (targetChanged)
    {
        state.initialized = true;
        state.targetEntry = current.targetEntry;
        state.questId = current.questId;
        state.mapId = current.mapId;
        state.zoneId = current.zoneId;
        state.x = current.x;
        state.y = current.y;
        state.distance = current.distance;
        state.lastProgressAt = current.now;
        state.recoveryStage = 0;
        return RecoveryAction::None;
    }

    // A real master, combat, a transport transition, or a corpse/revive
    // transition is not a failed route. Give normal movement a fresh stall
    // window once it resumes instead of consuming a recovery stage.
    if (current.paused)
    {
        state.lastProgressAt = current.now;
        return RecoveryAction::None;
    }

    float const minimumSquaredDistance = minimumProgressDistance * minimumProgressDistance;
    bool const progressed = state.mapId != current.mapId || state.zoneId != current.zoneId ||
        SquaredDistance(state.x, state.y, current.x, current.y) >= minimumSquaredDistance ||
        current.distance + minimumProgressDistance <= state.distance;

    if (progressed)
    {
        state.mapId = current.mapId;
        state.zoneId = current.zoneId;
        state.x = current.x;
        state.y = current.y;
        state.distance = current.distance;
        state.lastProgressAt = current.now;
        state.recoveryStage = 0;
        return RecoveryAction::None;
    }

    if (current.now - state.lastProgressAt < stallSeconds)
        return RecoveryAction::None;

    state.lastProgressAt = current.now;
    if (state.recoveryStage++ == 0)
        return RecoveryAction::RecomputeRoute;

    state.suppressedEntry = current.targetEntry;
    state.suppressedMapId = current.mapId;
    state.suppressUntil = current.now + cooldownSeconds;
    return RecoveryAction::SuppressRouteAndCooldown;
}

inline bool IsSuppressed(State const& state, std::uint32_t now, std::uint32_t entry, std::uint32_t mapId)
{
    return state.suppressUntil > now && state.suppressedEntry == entry && state.suppressedMapId == mapId;
}
}
