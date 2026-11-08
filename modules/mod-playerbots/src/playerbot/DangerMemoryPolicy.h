#pragma once

#include "CorpseRunFirstPolicy.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ai::danger_memory
{
// twow-repo#575 A3 step 4 S4-4 (AiPlayerbot.Danger.Memory, OB-00 07.11.2026; design
// ws-10/cli-pathfinding/a3-step4-route/DESIGN-STEP4-RESCUE-DANGER-20261107.md d54dfd0e, addendum 2 be21560e): danger memory per
// place, one read function DangerAt (the name the step-3 planner uses, A3-S3 §1.8). Pure rules, no game types, no config reads,
// no logging.
//
// Two layers:
// - per bot: the bot's own deaths (the S4-1 ring, ai::corpse_first::Ring; bot thread only, not cleared by repop);
// - shared: the DangerMap cells at the point and its 8 neighbours (DangerMap::QueryAt), judged by the REAL killer (the highest
//   living attacker at the death, Death::realKillerLevel) instead of the bot's "current target". The legacy readers keep their
//   killer and stay unchanged (the map is shared by both cohorts).
// It also fixes design F4: the far-move arrival check queried from == to, and Query skips the start cell and its neighbours,
// so it never saw anything.

constexpr std::uint32_t OwnWindowSeconds = 1800;   // own deaths within 30 min ...
constexpr float OwnRadius = 60.0f;                 // ... and 60 yd (2D, CampBlock radius) of the point
constexpr std::uint32_t OwnAvoidDeaths = 2;        // A3 4.5 (1): a place with several deaths of the same bot is avoided
constexpr std::uint32_t LinesPerMinute = 20;       // realm-wide [Danger] lines

// Cohort mode (CohortPolicy.h): 0 off, 1 all, 2 cohort B, 3 cohort A; anything else is off.
inline std::uint32_t ClampMode(int value)
{
    return value >= 0 && value <= 3 ? std::uint32_t(value) : 0u;
}

enum class Level : std::uint8_t
{
    Safe = 0,
    Caution,   // logged only in this pair; kept for the planner's ranking
    Avoid
};

inline char const* LevelName(Level level)
{
    switch (level)
    {
        case Level::Safe: return "safe";
        case Level::Caution: return "caution";
        case Level::Avoid: return "avoid";
        default: return "unknown";
    }
}

enum class Query : std::uint8_t
{
    Target = 0,   // travel target destination (ChooseTravelTargetAction, the #307 deferral)
    Arrive,       // far-move arrival point (FarMoveArrivalPoint, F4)
    Grind,        // grind target position (GrindTargetValue)
    Count
};

inline char const* QueryName(Query q)
{
    switch (q)
    {
        case Query::Target: return "target";
        case Query::Arrive: return "arrive";
        case Query::Grind: return "grind";
        default: return "unknown";
    }
}

// Own deaths within OwnWindowSeconds before now and OwnRadius (2D) of the point, same map.
inline std::uint32_t OwnDeathsNear(ai::corpse_first::Ring const& ring, std::uint32_t nowSeconds, std::uint32_t mapId, float x, float y)
{
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < ring.count; ++i)
    {
        ai::corpse_first::DeathMark const& d = ring.marks[i];
        if (d.mapId != mapId || d.atSeconds > nowSeconds || nowSeconds - d.atSeconds > OwnWindowSeconds)
            continue;
        float const dx = d.x - x, dy = d.y - y;
        if (dx * dx + dy * dy <= OwnRadius * OwnRadius)
            ++n;
    }
    return n;
}

// The level of a place: own deaths >= 2 or a dangerous shared cell = Avoid; one own death = Caution.
inline Level Combine(std::uint32_t ownDeaths, std::uint32_t sharedCells)
{
    if (ownDeaths >= OwnAvoidDeaths || sharedCells > 0)
        return Level::Avoid;
    if (ownDeaths > 0)
        return Level::Caution;
    return Level::Safe;
}

// The shared answer depends only on the DangerMap cell of the point (QueryAt checks that cell and its neighbours), so a caller that
// asks for many points in one pass (the grind target loop) keeps it per cell: one lock per cell instead of one per candidate.
struct SharedCellCache
{
    struct Entry
    {
        std::uint32_t map = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t cells = 0;
    };
    static constexpr std::size_t Size = 16;
    std::array<Entry, Size> entries{};
    std::size_t count = 0;

    bool Find(std::uint32_t map, std::int32_t x, std::int32_t y, std::uint32_t& cells) const
    {
        for (std::size_t i = 0; i < count; ++i)
            if (entries[i].map == map && entries[i].x == x && entries[i].y == y)
            {
                cells = entries[i].cells;
                return true;
            }
        return false;
    }

    // Full: nothing is kept (the caller asks the map again), so the answer is never stale within the pass.
    void Put(std::uint32_t map, std::int32_t x, std::int32_t y, std::uint32_t cells)
    {
        if (count < Size)
            entries[count++] = Entry{ map, x, y, cells };
    }
};

// Realm-wide counters for [DangerMin] (relaxed atomics; sums over a window are exact) and the [Danger] line budget.
struct Counters
{
    std::array<std::atomic<std::uint32_t>, std::size_t(Query::Count)> avoid{};
    std::atomic<std::uint32_t> caution{0};
    std::atomic<std::uint32_t> ownAvoid{0};      // Avoid with own deaths >= 2
    std::atomic<std::uint32_t> sharedAvoid{0};   // Avoid with a shared cell
    std::atomic<std::uint32_t> realKillerRecorded{0};
    std::atomic<std::uint32_t> lines{0};
    std::atomic<std::uint64_t> lastLogSeconds{0};

    void Note(Query q, Level level, std::uint32_t ownDeaths, std::uint32_t sharedCells)
    {
        if (level == Level::Caution)
            caution.fetch_add(1, std::memory_order_relaxed);
        if (level != Level::Avoid)
            return;
        if (q < Query::Count)
            avoid[std::size_t(q)].fetch_add(1, std::memory_order_relaxed);
        if (ownDeaths >= OwnAvoidDeaths)
            ownAvoid.fetch_add(1, std::memory_order_relaxed);
        if (sharedCells > 0)
            sharedAvoid.fetch_add(1, std::memory_order_relaxed);
    }

    // A [Danger] line may be written (LinesPerMinute per minute; the minute line resets the budget).
    bool LineAllowed()
    {
        return lines.fetch_add(1, std::memory_order_relaxed) < LinesPerMinute;
    }

    // True once per minute for one caller; that caller writes [DangerMin] and resets the line budget.
    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t last = lastLogSeconds.load(std::memory_order_relaxed);
        if (nowSeconds < last + 60)
            return false;
        if (!lastLogSeconds.compare_exchange_strong(last, nowSeconds, std::memory_order_relaxed))
            return false;
        lines.store(0, std::memory_order_relaxed);
        return true;
    }
};

inline Counters& SharedCounters()
{
    static Counters counters;
    return counters;
}
}
