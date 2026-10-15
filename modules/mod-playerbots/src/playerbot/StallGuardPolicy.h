#pragma once

#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <unordered_map>

namespace ai::stall_guard
{
// twow-repo#416 (train 7, 2026-09-28 04:06Z): map 0 blocked 21 s in the player
// updates. The bot could not be identified (no per-bot timing), so this adds
// (a) a rate-limited [BotSlowUpdate] line for any bot update above a threshold
// and (b) a time budget for the most expensive bot routine that can run long
// in one call: TravelNodeMap::getRoute(position) tries up to 5 x 5 node pairs,
// each with an A* and mmap path searches. Limits are generous: a normal route
// takes milliseconds (map 0 has 634 travel nodes), the budget only stops runs
// that would block the map thread.

constexpr std::uint32_t SlowUpdateMs = 1000;          // log a bot update above this
constexpr std::uint32_t SlowUpdateLogSeconds = 60;    // at most one line per bot and minute
constexpr std::uint32_t RouteBudgetMs = 1000;         // one getRoute(position) call
constexpr std::uint32_t RouteCooldownSeconds = 300;   // same bot, same target cell: no retry
constexpr float RouteCellSize = 100.0f;               // yards per target cell

inline bool ShouldLog(std::uint32_t lastLogSeconds, std::uint32_t nowSeconds, std::uint32_t intervalSeconds)
{
    return lastLogSeconds == 0 || nowSeconds - lastLogSeconds >= intervalSeconds;
}

inline bool BudgetExceeded(std::uint32_t elapsedMs, std::uint32_t budgetMs)
{
    return budgetMs && elapsedMs > budgetMs;
}

// twow-repo#592 (PATH-02 phase 2, group A no. 6, AiPlayerbot.Perf.RouteFailEarlySkip, default 0): during a route-fail
// cooldown every MoveTo of the blocked bot still pays the direct navmesh attempt, the node map lock and getNodes before
// getRoute sees the cooldown (up to 118 ms a minute for a bot stuck at a far target). The cooldown key holds no start,
// and the direct attempt may succeed after a move, so it cannot simply go first. A failure cooldown set by a getFullPath
// search keeps where that search started, the exact target and how far its direct attempt got; a later search of the
// same bot leaves out the direct attempt only while start and target each moved at most EarlySkipYards and that
// attempt ended far from the spell-distance goal. A stored gap below 0 (no direct path, or none measured) never matches.
constexpr float EarlySkipYards = 1.0f;
constexpr float EarlySkipGapMargin = 5.0f;   // stored direct_gap at least spell distance + this
constexpr std::uint32_t RouteFailEarlySkipMax = 2;   // 0 off, 1 shadow, 2 active

inline std::uint32_t ClampRouteFailEarlySkip(std::int32_t level)
{
    if (level <= 0)
        return 0;
    if (level > std::int32_t(RouteFailEarlySkipMax))
        return RouteFailEarlySkipMax;
    return std::uint32_t(level);
}

// Where a failed getFullPath search started, its exact target and its direct attempt's gap. known = false for every
// other cooldown (budget stop, getRoute callers without a direct attempt): those never allow an early skip.
struct RouteFailOrigin
{
    bool known = false;
    std::uint32_t startMap = 0;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float directGap = -1.0f;   // below 0 = no direct path / not measured: never an early skip
};

inline RouteFailOrigin MakeRouteFailOrigin(std::uint32_t startMap, float sx, float sy, float sz, float tx, float ty, float tz, float directGap)
{
    RouteFailOrigin origin;
    origin.known = true;
    origin.startMap = startMap;
    origin.sx = sx;
    origin.sy = sy;
    origin.sz = sz;
    origin.tx = tx;
    origin.ty = ty;
    origin.tz = tz;
    origin.directGap = directGap;
    return origin;
}

// The search now (start, target) repeats the stored failure closely enough to give the same direct attempt result.
inline bool EarlySkipMatches(RouteFailOrigin const& stored, RouteFailOrigin const& now, float minDirectGap)
{
    if (!stored.known || stored.startMap != now.startMap)
        return false;
    float const dsx = stored.sx - now.sx, dsy = stored.sy - now.sy, dsz = stored.sz - now.sz;
    float const dtx = stored.tx - now.tx, dty = stored.ty - now.ty, dtz = stored.tz - now.tz;
    float const reach = EarlySkipYards * EarlySkipYards;
    if (dsx * dsx + dsy * dsy + dsz * dsz > reach || dtx * dtx + dty * dty + dtz * dtz > reach)
        return false;
    return stored.directGap >= 0.0f && stored.directGap >= minDirectGap;
}

// Bot + map + target cell whose route search ran over budget: skipped for a
// while so a failing search does not turn into many small stalls. Bounded and
// mutex-protected (map threads, #351).
class RouteCooldownStore
{
public:
    static constexpr std::size_t MaxEntries = 4096;

    // Bot GUID (32 bit) and target cell (map 12 bit, x and y 10 bit each; world
    // coordinates stay within +-20000 yards, i.e. +-200 of the 512 cells per axis).
    static std::uint64_t Key(std::uint32_t botGuid, std::uint32_t mapId, float x, float y)
    {
        std::uint32_t const cx = std::uint32_t(std::int32_t(x / RouteCellSize) + 512) & 0x3FF;
        std::uint32_t const cy = std::uint32_t(std::int32_t(y / RouteCellSize) + 512) & 0x3FF;
        std::uint32_t const cell = ((mapId & 0xFFF) << 20) | (cx << 10) | cy;
        return (std::uint64_t(botGuid) << 32) | cell;
    }

    // #592: origin only from a failed getFullPath search (route-fail cooldown); every other block keeps it unknown.
    void Block(std::uint64_t key, std::uint32_t untilSeconds, std::uint32_t nowSeconds, RouteFailOrigin const& origin = RouteFailOrigin())
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (entries.size() >= MaxEntries)
        {
            for (auto it = entries.begin(); it != entries.end();)
                it = it->second.until <= nowSeconds ? entries.erase(it) : std::next(it);
            if (entries.size() >= MaxEntries)
                entries.clear();
        }
        entries[key] = Entry{ untilSeconds, origin };
    }

    bool IsBlocked(std::uint64_t key, std::uint32_t nowSeconds) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = entries.find(key);
        return it != entries.end() && it->second.until > nowSeconds;
    }

    // #592: the key is blocked and its stored failure matches the search now (EarlySkipMatches); outDirectGap = the
    // stored gap for the caller's detail.
    bool EarlySkip(std::uint64_t key, std::uint32_t nowSeconds, RouteFailOrigin const& now, float minDirectGap, float& outDirectGap) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = entries.find(key);
        if (it == entries.end() || it->second.until <= nowSeconds || !EarlySkipMatches(it->second.origin, now, minDirectGap))
            return false;
        outDirectGap = it->second.origin.directGap;
        return true;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entries.size();
    }

private:
    struct Entry
    {
        std::uint32_t until = 0;
        RouteFailOrigin origin;
    };

    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, Entry> entries;
};

inline RouteCooldownStore& RouteCooldowns()
{
    static RouteCooldownStore store;
    return store;
}

// twow-repo#541 (L1b, AiPlayerbot.Perf.RouteFailCooldownSeconds, default 0): chain42 (11.10.2026) L0 trace - 67 % of
// the slow route builds at 1600 bots were getRoute(position) searches that tried every node pair (60-150 ms) and found
// no route, and the same bot repeated the same search, because the cooldown above only follows an exceeded budget
// (budget_exceeded=0 in all arms). With the switch a search without a route blocks the same bot and target cell for
// that many seconds; the caller gets the same empty route as from the search (MoveTo: the same straight-line fallback).
constexpr std::uint32_t RouteFailCooldownMaxSeconds = 300;

inline std::uint32_t ClampRouteFailCooldownSeconds(std::int32_t seconds)
{
    if (seconds <= 0)
        return 0;
    if (seconds > std::int32_t(RouteFailCooldownMaxSeconds))
        return RouteFailCooldownMaxSeconds;
    return std::uint32_t(seconds);
}

// [RouteFail] counters (only counted with the switch on). Map threads add, the world thread takes them once per
// minute. blocks = searches without a route that set a cooldown, skips = searches skipped by any route cooldown.
inline std::atomic<std::uint64_t> routeFailBlocks{ 0 };
inline std::atomic<std::uint64_t> routeCooldownSkips{ 0 };
// twow-repo#592 (AiPlayerbot.Perf.RouteFailEarlySkip): early_skips = searches whose stored failure matched (shadow: they
// still ran, active: left out before the direct attempt; a subset of skips when active), early_wrong = shadow matches
// whose direct attempt then reached spell distance (a direct path the active level would have lost).
inline std::atomic<std::uint64_t> routeEarlySkips{ 0 };
inline std::atomic<std::uint64_t> routeEarlyWrong{ 0 };
}

#include <array>
#include <string>

namespace ai::stall_guard
{
// #416 (7.2): the three slowest actions of one bot update, for [BotSlowUpdate].
// Only actions that took at least 1 ms are recorded.
struct TopActions
{
    struct Entry
    {
        std::string name;
        uint32_t ms = 0;
    };

    std::array<Entry, 3> top;

    void Reset()
    {
        for (Entry& entry : top)
        {
            entry.name.clear();
            entry.ms = 0;
        }
    }

    void Add(std::string const& name, uint32_t ms)
    {
        if (!ms)
            return;

        size_t slot = top.size();
        while (slot > 0 && ms > top[slot - 1].ms)
            --slot;
        if (slot == top.size())
            return;

        for (size_t i = top.size() - 1; i > slot; --i)
            top[i] = top[i - 1];
        top[slot] = Entry{ name, ms };
    }

    // "name:ms,name:ms"
    std::string Format() const
    {
        std::string out;
        for (Entry const& entry : top)
        {
            if (!entry.ms)
                break;
            if (!out.empty())
                out += ',';
            out += entry.name + ':' + std::to_string(entry.ms);
        }
        return out;
    }
};
}
