#pragma once

#include <array>
#include <cstdint>

namespace ai::turnin_recovery
{
enum class RecoveryAction : std::uint8_t
{
    None,
    RecomputeRoute,
    SuppressRouteAndCooldown,
    SuppressTransportAndCooldown    // hotfix 8.13: target on another map, no better distance (TransportStallPolicy.h)
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
    // twow-repo#485: map of the travel target. mapId (the bot's map) drives the
    // progress check; a stall suppression is keyed by the target's map, the
    // one the route choice asks about, so it also holds for cross-map turn-ins.
    std::uint32_t targetMapId = 0;
    // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall + TurnInNearRadius): the near zone around the
    // taker, in yards. 0 = off: Observe judges progress as before.
    float nearRadius = 0.0f;
};

// twow-repo#601 S3c (OB-10 rev. 2, Q-B3): inside the near zone only getting this much closer than the best
// distance so far counts as progress.
constexpr float NearProgressDistance = 3.0f;

// The near zone: switched on (radius > 0), the bot on the taker's map and within the radius (3D distance).
inline bool InNearZone(float distance, bool sameMap, float nearRadius)
{
    return nearRadius > 0.0f && sameMap && distance <= nearRadius;
}

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
    // twow-repo#601 S3c: the best (smallest) distance at a progress point of this target. Set with the target,
    // lowered by each progress (reset by a map change), so a bot that drifts out of the near zone and back in is not
    // "closer" again.
    float bestDistance = 0.0f;
    std::uint32_t lastProgressAt = 0;
    std::uint8_t recoveryStage = 0;
    std::uint32_t suppressedEntry = 0;
    std::uint32_t suppressedMapId = 0;
    std::uint32_t suppressUntil = 0;
    // twow-repo#485: the death cooldown (#307, an hour live) has its own slot,
    // so a stall on another target (two minutes live) never overwrites it.
    std::uint32_t deathSuppressedEntry = 0;
    std::uint32_t deathSuppressedMapId = 0;
    std::uint32_t deathSuppressedUntil = 0;
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
    state.deathSuppressedEntry = targetEntry;
    state.deathSuppressedMapId = mapId;
    state.deathSuppressedUntil = now + cooldownMs;
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
        state.bestDistance = current.distance;
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

    // twow-repo#601 S3c (Azora, map-z4.md 3.3): near the taker the move picks a new random point around it on every
    // call, so the bot shifts 25 yd and more without getting closer, and the 2D displacement rule restarted the stall
    // window again and again. In the near zone only a new best distance NearProgressDistance below the best so far
    // (or a map change) is progress. Outside it, or with nearRadius 0, nothing changes.
    bool const mapChanged = state.mapId != current.mapId;
    bool const nearZone = InNearZone(current.distance, current.mapId == current.targetMapId, current.nearRadius);
    float const minimumSquaredDistance = minimumProgressDistance * minimumProgressDistance;
    bool const progressed = nearZone ? mapChanged || current.distance + NearProgressDistance <= state.bestDistance :
        mapChanged || state.zoneId != current.zoneId ||
        SquaredDistance(state.x, state.y, current.x, current.y) >= minimumSquaredDistance ||
        current.distance + minimumProgressDistance <= state.distance;

    if (progressed)
    {
        if (mapChanged || current.distance < state.bestDistance)
            state.bestDistance = current.distance;
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
    state.suppressedMapId = current.targetMapId;
    state.suppressUntil = current.now + cooldownSeconds;
    return RecoveryAction::SuppressRouteAndCooldown;
}

// A route is suppressed by a stall (above) or by deaths (RecordDeathOnRoute).
inline bool IsSuppressed(State const& state, std::uint32_t now, std::uint32_t entry, std::uint32_t mapId)
{
    bool const stalled = state.suppressUntil > now && state.suppressedEntry == entry && state.suppressedMapId == mapId;
    bool const died = state.deathSuppressedUntil > now && state.deathSuppressedEntry == entry &&
        state.deathSuppressedMapId == mapId;
    return stalled || died;
}

// #329: a target that is dropped and picked again every few seconds must not
// restart the stall window, or the recovery above never runs (live: one bot
// re-selected the same turn-in 96 times at a constant 223 yards). Only a
// different quest target starts fresh; a null or non-quest target in between,
// and the same entry + quest again, keep the observed progress.
inline bool ShouldResetProgressOnSetTarget(State const& state, bool questTarget,
    std::uint32_t entry, std::uint32_t questId)
{
    if (!questTarget || !state.initialized)
        return false;

    return state.targetEntry != entry || state.questId != questId;
}

// twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall, OB-10 rev. 2 Q-B4): the quests of this bot's last work
// timeouts at a taker (soak: bot 216 reached the Azora taker twice, at 2-4 yd, and both work phases ran out). At most
// WorkTimeoutRingSize quests (the oldest gives way), in memory only like the S1 abandon mark. Quest 0 is never kept.
constexpr std::uint32_t WorkTimeoutRingSize = 8;

struct WorkTimeoutRing
{
    std::array<std::uint32_t, WorkTimeoutRingSize> quests{};
    std::uint32_t next = 0;
};

// Records a work timeout of questId's turn-in. True for the second one of the same quest still in the ring (the caller
// parks the turn-in); the quest then leaves the ring, so a later timeout starts a new pair.
inline bool RecordWorkTimeout(WorkTimeoutRing& ring, std::uint32_t questId)
{
    if (!questId)
        return false;

    for (std::uint32_t& quest : ring.quests)
    {
        if (quest == questId)
        {
            quest = 0;
            return true;
        }
    }

    ring.quests[ring.next % WorkTimeoutRingSize] = questId;
    ring.next = (ring.next + 1) % WorkTimeoutRingSize;
    return false;
}
}
