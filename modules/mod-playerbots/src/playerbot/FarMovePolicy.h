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
    std::atomic<std::uint32_t> arriveDeaths{0};  // hotfix 8.36: deaths within ArriveDeathSeconds after a far-move arrival
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
constexpr std::uint32_t ArriveDeathSeconds = 60;  // a death this soon after arriving counts as arrive_deaths

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

// twow-repo#601 cand4-B (AiPlayerbot.RosterFarMove.ArriveSafe, OB-00 28.10.2026): 382 of 1022 deaths in 3 h came within 60 s of a
// far-move arrival. The arrival point lies ArriveBackYards before a kill objective - usually inside its camp - and only
// danger-map cells were refused, so the bot appeared within the aggro radius of several mobs and was pulled without an
// approach (killers relatively 1.2 levels higher, more adds, more HP left). With the switch the arrival is checked right
// before the teleport: a point where a creature would attack the bot at once (core proximity aggro, Creature.cpp
// GetAttackDistance, AggressorAI / CreatureEventAI MoveInLineOfSight, without the LOS test) is given up for one ArriveSafeStep
// further back along the line, at most ArriveSafeSteps times; the bot walks the rest and pulls on its own.
constexpr float ArriveSafeStepYards = 30.0f;
constexpr std::uint32_t ArriveSafeSteps = 4;
constexpr float ArriveSafeMarginYards = 5.0f;   // on top of the creature's aggro radius
constexpr float ArriveSafeSearchYards = 60.0f;  // grid search radius: above the largest proximity aggro radius (about 45 yd)
constexpr float ArriveSafeZYards = 3.0f;        // CREATURE_Z_ATTACK_RANGE: a creature that cannot fly ignores a bot beyond it

// A creature at distance / height difference dz from the arrival point that would attack the bot there at once: it can start
// an attack (alive, hostile, not neutral to all, not stunned or passive, no victim - the caller checks those), the point is
// inside its aggro radius attackDistance (0 = it never aggroes) plus the margin, and within its z range unless it flies.
inline bool Threatens(float distance, float attackDistance, float dz, bool canFly)
{
    return attackDistance > 0.0f && distance <= attackDistance + ArriveSafeMarginYards && (canFly || dz <= ArriveSafeZYards);
}

// One arrival candidate, k steps back (k = 0: the point chosen at the start of the far move).
struct ArriveCandidate
{
    bool valid = false;           // on the navmesh, outside danger-map cells, its grid loaded
    std::uint32_t threats = 0;    // creatures that would attack the bot there (Threatens)
};

enum class ArriveOutcome : std::uint8_t
{
    Kept,       // the first candidate is free
    Shifted,    // a later candidate is free
    Unsafe,     // none is free: the farthest valid one (no endless search)
    None        // no valid candidate: the caller keeps its point (as before the switch)
};

struct ArriveChoice
{
    ArriveOutcome outcome = ArriveOutcome::None;
    std::uint32_t index = 0;
};

// The first free valid candidate, else the farthest valid one.
inline ArriveChoice ChooseArrival(ArriveCandidate const* candidates, std::uint32_t count)
{
    ArriveChoice choice;
    bool anyValid = false;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!candidates[i].valid)
            continue;
        if (!candidates[i].threats)
        {
            choice.outcome = i == 0 ? ArriveOutcome::Kept : ArriveOutcome::Shifted;
            choice.index = i;
            return choice;
        }
        anyValid = true;
        choice.index = i;   // the farthest valid one so far
    }
    choice.outcome = anyValid ? ArriveOutcome::Unsafe : ArriveOutcome::None;
    if (!anyValid)
        choice.index = 0;
    return choice;
}

// The [FarMove] counters of the switch (shared by all map threads): arrivals checked, kept, shifted, unsafe, not checked
// (the arrival grid not loaded: no creature there to see yet), and the check cost.
struct ArriveSafeCounters
{
    std::atomic<std::uint32_t> checked{0};
    std::atomic<std::uint32_t> kept{0};
    std::atomic<std::uint32_t> shifted{0};
    std::atomic<std::uint32_t> unsafe{0};
    std::atomic<std::uint32_t> unchecked{0};
    std::atomic<std::uint64_t> checkUs{0};
    std::atomic<std::uint32_t> checkUsMax{0};

    void NoteCost(std::uint32_t us)
    {
        checkUs.fetch_add(us, std::memory_order_relaxed);
        std::uint32_t seen = checkUsMax.load(std::memory_order_relaxed);
        while (us > seen && !checkUsMax.compare_exchange_weak(seen, us, std::memory_order_relaxed))
        {
        }
    }
};

inline ArriveSafeCounters& SharedArriveSafeCounters() { static ArriveSafeCounters counters; return counters; }
}
