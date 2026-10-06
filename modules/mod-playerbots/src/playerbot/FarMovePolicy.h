#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace ai::far_move
{
// Hotfix 8.33a (twow-repo#544): with AiPlayerbot.RosterFarMove a roster bot without a player at its
// start or at its target (same map, farther than react distance) does not compute a route at all
// (MoveTo2's ResolveMovePath was the cost of "move to travel target", 11 ms per tick on the v37 test
// realm). It waits the walking time and then appears at the target (8.33b: other moves in between do
// not cancel the trip, only another far target does). The wait is capped below the
// turn-in stall window (300 s without 25 yd progress), so a long trip never looks like a stall.
constexpr std::uint32_t MaxWaitSeconds = 240;
constexpr float SameTargetYards = 30.0f;  // hotfix 8.33b: same trip despite small target shifts

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
    std::atomic<std::uint32_t> retargeted{0};  // hotfix 8.33b: a pending far move replaced by another far target
    std::atomic<std::uint32_t> arrived{0};
    std::atomic<std::uint32_t> watched{0};     // hotfix 8.36: a player near the route - walked instead
    std::atomic<std::uint32_t> arriveAlt{0};   // hotfix 8.36: first arrival point in danger, second one used
    std::atomic<std::uint32_t> arriveWalk{0};  // hotfix 8.36: no safe arrival point (danger / no ground) - walked
    std::atomic<std::uint64_t> lastLogMinute{0};

    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t const minute = nowSeconds / 60;
        std::uint64_t seen = lastLogMinute.load(std::memory_order_relaxed);
        return seen != minute && lastLogMinute.compare_exchange_strong(seen, minute, std::memory_order_relaxed);
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }

// Hotfix 8.36 (twow-repo#544, owner 06.10.2026: "wenn der bot direkt vor hogger spawnt ist auch mau",
// "bots auf weges routen entgegen kommen zu sehen hat schon was"):
// - the far move ends ArriveBackYards before the target, on the straight line from the start; the bot
//   walks the rest with path finding and its normal awareness of mobs;
// - a real player within visibility of the straight route (not only at its ends) sees the bot walk.
constexpr float ArriveAltYards = 120.0f;   // second try when the first arrival point is in danger

struct Point2
{
    float x = 0.0f;
    float y = 0.0f;
};

// Distance of p to the segment a-b (2D).
inline float SegmentDistance(Point2 p, Point2 a, Point2 b)
{
    float const dx = b.x - a.x, dy = b.y - a.y;
    float const len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float const cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

// The point backYards before the target, on the line target -> start (2D; the caller corrects the
// height on the navmesh). A route not longer than backYards gives the start itself.
inline Point2 ArrivalPoint(Point2 start, Point2 target, float backYards)
{
    float const dx = start.x - target.x, dy = start.y - target.y;
    float const len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f || backYards <= 0.0f)
        return target;
    float const f = backYards >= len ? 1.0f : backYards / len;
    return Point2{ target.x + dx * f, target.y + dy * f };
}
}
