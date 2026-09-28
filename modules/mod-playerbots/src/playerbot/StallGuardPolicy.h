#pragma once

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

    void Block(std::uint64_t key, std::uint32_t untilSeconds, std::uint32_t nowSeconds)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (entries.size() >= MaxEntries)
        {
            for (auto it = entries.begin(); it != entries.end();)
                it = it->second <= nowSeconds ? entries.erase(it) : std::next(it);
            if (entries.size() >= MaxEntries)
                entries.clear();
        }
        entries[key] = untilSeconds;
    }

    bool IsBlocked(std::uint64_t key, std::uint32_t nowSeconds) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = entries.find(key);
        return it != entries.end() && it->second > nowSeconds;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entries.size();
    }

private:
    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, std::uint32_t> entries;
};

inline RouteCooldownStore& RouteCooldowns()
{
    static RouteCooldownStore store;
    return store;
}
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
