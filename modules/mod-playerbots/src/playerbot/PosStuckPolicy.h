#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

class Player;

// twow-repo#594 (AiPlayerbot.Path.PosStuck, OB-00 23.10.2026): the position guard. QFO-clean (23.10.2026): bot 1244 stood
// for 2 h 15 min at one spot in the Echo Ridge Mine with a validated route to its taker 335 yd away. Every fight (it shot
// kobolds from there) restarted the turn-in stall window, S3c only looks inside 60 yd of the taker, and the kills counted
// as progress for the rescue. The guard watches a travel target in TRAVEL: if the distance to it has not shrunk by
// progress yards net within the window - fights count into the window, but it never fires during a fight, only at the
// next observation after it - the bot is stuck on that target. The series of such episodes escalates per bot: park the
// target, then a local unstuck (back to the last point where the bot made progress), then the bot's home bind; the moves
// are rate-limited. The inline part is value-only like the turn-in recovery (it neither moves nor teleports a bot); the move
// itself is ApplyMove (PosStuckMove.cpp).
namespace ai::pos_stuck
{
constexpr std::uint32_t DefaultMinutes = 10;
constexpr float DefaultProgress = 5.0f;
constexpr std::uint32_t DefaultEpisodeMinutes = 30;
constexpr std::uint32_t DefaultRateMinutes = 30;
// A local unstuck goes back to the progress anchor only if it lies this far from the bot (closer: no help) and not
// farther than AnchorMaxDistance (farther: not local any more); otherwise the step goes to the home bind.
constexpr float AnchorMinDistance = 5.0f;
constexpr float AnchorMaxDistance = 300.0f;
// OB-10 review (23.10.2026): a move only if the bot itself stood - its net displacement over the window below this. A long
// detour (around a mountain or a lake) gets no closer either; it is parked, never moved.
constexpr float StillDisplacement = 20.0f;
constexpr std::uint32_t DefaultLinesPerMinute = 30;
// twow-repo#342 ABBA-1 §6 (OB-00 28.10.2026): a bot that waited for a ship or zeppelin at its dock within the last
// DockWaitPauseMs is held up on purpose - the dock wait (LastMovement::transportWaitMs, renewed about every second while it
// lasts) runs up to Path.TransportWaitTimeoutMin, beyond the window, and a detect parked or moved a bot that only waited.
constexpr std::uint32_t DockWaitPauseMs = 30000;

// waitMs = getMSTime of the last dock wait, 0 = none (wrapping millisecond clock).
inline bool DockWaitPaused(std::uint32_t nowMs, std::uint32_t waitMs, std::uint32_t pauseMs)
{
    return waitMs != 0 && std::uint32_t(nowMs - waitMs) < pauseMs;
}

// OB-10 review (28.10.2026): a pause has an upper limit. The dock wait is renewed on every pass, so a bot that waits for good
// (the transport never comes, or cannot be boarded) would never be watched again; after maxMs of continuous pause the window
// runs again, and a stand detects as before. since = the start of the continuous pause (0 = none), kept in State across the
// windows the pause clears (a start at clock 0 is stored as 1 ms before it); pausedNow = the pause condition at this observation.
inline bool PauseHolds(std::uint32_t& since, bool pausedNow, std::uint32_t nowMs, std::uint32_t maxMs)
{
    if (!pausedNow)
    {
        since = 0;
        return false;
    }
    if (!since)
        since = nowMs ? nowMs : 0xFFFFFFFFu;
    return std::uint32_t(nowMs - since) < maxMs;
}

struct Observation
{
    std::uint32_t now = 0;
    // The travel target (entry and quest; KeyOf). A new key starts a fresh window.
    std::uint64_t key = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float distance = 0.0f;   // bot -> travel target, same map
    bool combat = false;
    // Outside the population or held up on purpose (dead, taxi, transport, real master, battleground or instance, rest,
    // target on another map, a PartialNoop hit since the window began, a dock wait): no window runs.
    bool excluded = false;
};

struct State
{
    bool initialized = false;
    std::uint64_t key = 0;
    std::uint32_t mapId = 0;
    std::uint32_t windowStart = 0;
    float refDistance = 0.0f;   // distance at the window start; progress = refDistance - distance >= progress yards
    float windowX = 0.0f;       // the bot's position at the window start (2D)
    float windowY = 0.0f;
    float detectDisplacement = 0.0f;   // the bot's net displacement over the window of the last detect (yards, 2D)
    // The last point where the bot got progress yards closer to a target (kept across targets, cleared by a map change).
    bool hasAnchor = false;
    std::uint32_t anchorMapId = 0;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    float anchorZ = 0.0f;
    // The start of the continuous dock-wait pause (PauseHolds; 0 = none). Reset() leaves it alone: a pause clears the window.
    std::uint32_t dockPauseSince = 0;

    void Reset() { initialized = false; }
};

enum class Verdict : std::uint8_t
{
    None,
    Detect
};

inline std::uint64_t KeyOf(std::int32_t entry, std::uint32_t questId)
{
    return (std::uint64_t(std::uint32_t(entry)) << 32) | questId;
}

inline Verdict Observe(State& state, Observation const& current, std::uint32_t windowMs, float progress)
{
    if (current.excluded)
    {
        state.initialized = false;
        return Verdict::None;
    }

    if (state.mapId != current.mapId)
        state.hasAnchor = false;

    if (!state.initialized || state.key != current.key || state.mapId != current.mapId)
    {
        state.initialized = true;
        state.key = current.key;
        state.mapId = current.mapId;
        state.windowStart = current.now;
        state.refDistance = current.distance;
        state.windowX = current.x;
        state.windowY = current.y;
        return Verdict::None;
    }

    if (state.refDistance - current.distance >= progress)
    {
        state.windowStart = current.now;
        state.refDistance = current.distance;
        state.windowX = current.x;
        state.windowY = current.y;
        state.hasAnchor = true;
        state.anchorMapId = current.mapId;
        state.anchorX = current.x;
        state.anchorY = current.y;
        state.anchorZ = current.z;
        return Verdict::None;
    }

    if (current.now - state.windowStart < windowMs)
        return Verdict::None;

    // The window is over, but a fight is never interrupted: the next observation after it decides.
    if (current.combat)
        return Verdict::None;

    float const dx = current.x - state.windowX, dy = current.y - state.windowY;
    state.detectDisplacement = std::sqrt(dx * dx + dy * dy);
    state.windowStart = current.now;
    state.refDistance = current.distance;
    state.windowX = current.x;
    state.windowY = current.y;
    return Verdict::Detect;
}

// OB-10 review: the last detect came from a bot that stood (net displacement over its window below StillDisplacement) - only
// then may the episode move it.
inline bool StoodStill(State const& state)
{
    return state.detectDisplacement < StillDisplacement;
}

// Usable for a local unstuck: on the bot's map, between AnchorMinDistance and AnchorMaxDistance away (2D).
inline bool AnchorUsable(State const& state, std::uint32_t mapId, float x, float y)
{
    if (!state.hasAnchor || state.anchorMapId != mapId)
        return false;
    float const dx = state.anchorX - x, dy = state.anchorY - y;
    float const d2 = dx * dx + dy * dy;
    return d2 >= AnchorMinDistance * AnchorMinDistance && d2 <= AnchorMaxDistance * AnchorMaxDistance;
}

enum class Step : std::uint8_t
{
    Park,       // park / suppress the target only
    Unstuck,    // park + back to the progress anchor
    Teleport    // park + home bind
};

// The episodes of one bot. An episode within episodeMs of the previous one continues the series, a later one starts a new
// series. The 1st episode of a series parks, the 2nd adds a local unstuck, every later one the home bind - but a move only
// if the bot stood (mayMove, StoodStill) and the last move is rateMs ago (else the episode parks only and the series stays
// where it is).
struct Episodes
{
    std::uint32_t lastAt = 0;
    std::uint32_t lastMoveAt = 0;
    std::uint8_t count = 0;
    bool moved = false;
};

inline Step Escalate(Episodes& episodes, std::uint32_t now, std::uint32_t episodeMs, std::uint32_t rateMs, bool mayMove)
{
    if (episodes.count == 0 || now - episodes.lastAt > episodeMs)
        episodes.count = 0;
    episodes.lastAt = now;

    if (episodes.count == 0)
    {
        episodes.count = 1;
        return Step::Park;
    }

    if (!mayMove)
        return Step::Park;

    if (episodes.moved && now - episodes.lastMoveAt < rateMs)
        return Step::Park;

    if (episodes.count < 255)
        ++episodes.count;
    episodes.moved = true;
    episodes.lastMoveAt = now;
    return episodes.count == 2 ? Step::Unstuck : Step::Teleport;
}

inline char const* StepName(Step step)
{
    return step == Step::Unstuck ? "unstuck" : step == Step::Teleport ? "teleport" : "park";
}

// OB-10 review: after a #587 rebind the home bind can lie in another race's start area (QFO14: 4 rebinds to area 132), so a
// home bind counts only on the bot's own map and zone; anything else is skipped (zone 0 = unknown, skipped too).
inline bool BindNear(std::uint32_t bindMapId, std::uint32_t bindZoneId, std::uint32_t mapId, std::uint32_t zoneId)
{
    return bindZoneId != 0 && bindMapId == mapId && bindZoneId == zoneId;
}

enum class MoveResult : std::uint8_t
{
    None,      // Park: nothing to move
    Moved,
    BindFar    // Teleport skipped: the home bind is not on the bot's map and zone
};

// [PosStuck] minute summary and line budget, shared by all bots (lock-free).
struct Counters
{
    std::atomic<std::uint32_t> detect{0};
    std::atomic<std::uint32_t> unstuck{0};
    std::atomic<std::uint32_t> teleport{0};
    std::atomic<std::uint32_t> logged{0};
    std::atomic<std::uint64_t> minute{0};

    // Starts a new minute when due; true for exactly one caller, which then logs the previous minute's summary.
    bool Roll(std::uint64_t nowSeconds, std::uint32_t& outDetect, std::uint32_t& outUnstuck, std::uint32_t& outTeleport,
        std::uint32_t& outLogged)
    {
        std::uint64_t const now = nowSeconds / 60;
        std::uint64_t seen = minute.load(std::memory_order_relaxed);
        if (seen == now || !minute.compare_exchange_strong(seen, now, std::memory_order_relaxed))
            return false;
        outDetect = detect.exchange(0, std::memory_order_relaxed);
        outUnstuck = unstuck.exchange(0, std::memory_order_relaxed);
        outTeleport = teleport.exchange(0, std::memory_order_relaxed);
        outLogged = logged.exchange(0, std::memory_order_relaxed);
        return seen != 0;
    }

    // One more episode; true when its lines are still within this minute's budget.
    bool Take(std::uint32_t linesPerMinute)
    {
        detect.fetch_add(1, std::memory_order_relaxed);
        if (logged.fetch_add(1, std::memory_order_relaxed) < linesPerMinute)
            return true;
        logged.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }

// The move of an Unstuck (to x, y, z on the bot's map) or a Teleport (home bind, only when BindNear) step; Park moves nothing.
// Defined in PosStuckMove.cpp, the only file where the guard moves a bot (TravelMgr.cpp stays free of teleports).
MoveResult ApplyMove(Player* bot, Step step, float x, float y, float z);
}
