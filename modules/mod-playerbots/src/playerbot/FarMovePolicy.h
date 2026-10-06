#pragma once

#include <atomic>
#include <cstdint>

namespace ai::far_move
{
// Hotfix 8.33a (twow-repo#544): with AiPlayerbot.RosterFarMove a roster bot without a player at its
// start or at its target (same map, farther than react distance) does not compute a route at all
// (MoveTo2's ResolveMovePath was the cost of "move to travel target", 11 ms per tick on the v37 test
// realm). It waits the walking time and then appears at the target. The wait is capped below the
// turn-in stall window (300 s without 25 yd progress), so a long trip never looks like a stall.
constexpr std::uint32_t MaxWaitSeconds = 240;
constexpr float SameTargetYards = 5.0f;

inline std::uint32_t WaitSeconds(float distance, float speedYardsPerSecond)
{
    if (speedYardsPerSecond <= 0.0f)
        return MaxWaitSeconds;
    float const seconds = distance / speedYardsPerSecond;
    return seconds >= float(MaxWaitSeconds) ? MaxWaitSeconds : std::uint32_t(seconds);
}

enum class Step { Start, Wait, Arrive };

// pendingAt == 0: no far move pending. sameTarget: the pending one goes where the bot wants to go now.
inline Step Next(std::uint64_t now, std::uint64_t pendingAt, bool sameTarget)
{
    if (!pendingAt || !sameTarget)
        return Step::Start;
    return now >= pendingAt ? Step::Arrive : Step::Wait;
}

// [FarMove] counters, one line a minute (shared by all map threads).
struct Counters
{
    std::atomic<std::uint32_t> started{0};
    std::atomic<std::uint32_t> arrived{0};
    std::atomic<std::uint64_t> lastLogMinute{0};

    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t const minute = nowSeconds / 60;
        std::uint64_t seen = lastLogMinute.load(std::memory_order_relaxed);
        return seen != minute && lastLogMinute.compare_exchange_strong(seen, minute, std::memory_order_relaxed);
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }
}
