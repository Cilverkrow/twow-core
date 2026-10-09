#pragma once

#include <atomic>
#include <cstdint>

namespace ai::startup_travel
{
// twow-repo#540 (WS10-STARTUP-TRAVEL-BURST-01): after a server start with BotBrain, many roster bots
// started long journeys at once; their move updates (node route / path finder in MovementAction::MoveTo)
// piled up in the map thread for 10-12 minutes (test realm a3/b3: 650-860 ticks/min, over100 ~150-200,
// "move to travel target" up to 416 ms). Two switches spread that start:
//  - Jitter: a roster bot on its own starts its first journey 0..JitterSeconds after its login,
//    by guid, so 180 bots do not all start in the same minute.
//  - Budget: in the first WindowSeconds of uptime at most MaxLongMovesPerSlot long moves (another
//    map or farther than LongMoveYards, without a cached route to the target) start per 100 ms slot,
//    across all map threads; the rest wait for a later slot (no move, no retry, no cooldown).
// 0 switches either part off.

constexpr float LongMoveYards = 100.0f;
constexpr std::uint64_t SlotMs = 100;

// Delay of the first journey after login, spread evenly over 0..maxSeconds by guid.
inline std::uint32_t JitterSeconds(std::uint32_t guid, std::uint32_t maxSeconds)
{
    if (!maxSeconds)
        return 0;
    return std::uint32_t(((std::uint64_t(guid) * 2654435761ULL) >> 8) % (std::uint64_t(maxSeconds) + 1));
}

inline bool JitterDone(std::uint64_t now, std::uint64_t loginTime, std::uint32_t guid, std::uint32_t maxSeconds)
{
    return now >= loginTime + JitterSeconds(guid, maxSeconds);
}

inline bool InWindow(std::uint32_t uptimeSeconds, std::uint32_t windowSeconds)
{
    return windowSeconds && uptimeSeconds < windowSeconds;
}

inline bool IsLongMove(bool sameMap, float distance, bool cachedRouteToTarget)
{
    return !cachedRouteToTarget && (!sameMap || distance > LongMoveYards);
}

// Shared by all map threads; approximate under races (a slot may admit one or two more), which is fine
// for a load cap.
class LongMoveBudget
{
public:
    bool TryTake(std::uint64_t nowMs, std::uint32_t maxPerSlot)
    {
        if (!maxPerSlot)
            return true;
        std::uint64_t const slot = nowMs / SlotMs;
        std::uint64_t seen = slot_.load(std::memory_order_relaxed);
        if (seen != slot && slot_.compare_exchange_strong(seen, slot, std::memory_order_relaxed))
            used_.store(0, std::memory_order_relaxed);
        return used_.fetch_add(1, std::memory_order_relaxed) < maxPerSlot;
    }

private:
    std::atomic<std::uint64_t> slot_{0};
    std::atomic<std::uint32_t> used_{0};
};

// Deferral counters for the [StartupTravel] line (at most once a minute).
struct Counters
{
    std::atomic<std::uint32_t> jitter{0};
    std::atomic<std::uint32_t> budget{0};
    std::atomic<std::uint64_t> lastLogMinute{0};

    // True for exactly one caller per new minute; that caller logs and resets.
    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t const minute = nowSeconds / 60;
        std::uint64_t seen = lastLogMinute.load(std::memory_order_relaxed);
        return seen != minute && lastLogMinute.compare_exchange_strong(seen, minute, std::memory_order_relaxed);
    }
};

// One budget and one set of counters for the whole server (inline: a single instance across TUs).
inline LongMoveBudget& SharedBudget() { static LongMoveBudget budget; return budget; }
inline Counters& SharedCounters() { static Counters counters; return counters; }

// twow-repo#541 (budget (b)): the same cap after the startup window, with its own instance and limit
// (AiPlayerbot.LongMoveBudget.MaxPerSlot), counted for the [LongMoveBudget] minute line.
struct RuntimeBudgetCounters
{
    std::atomic<std::uint32_t> taken{0};
    std::atomic<std::uint32_t> deferred{0};
    std::atomic<std::uint64_t> lastLogMinute{0};

    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t const minute = nowSeconds / 60;
        std::uint64_t seen = lastLogMinute.load(std::memory_order_relaxed);
        return seen != minute && lastLogMinute.compare_exchange_strong(seen, minute, std::memory_order_relaxed);
    }
};

inline LongMoveBudget& RuntimeBudget() { static LongMoveBudget budget; return budget; }
inline RuntimeBudgetCounters& RuntimeCounters() { static RuntimeBudgetCounters counters; return counters; }
}
