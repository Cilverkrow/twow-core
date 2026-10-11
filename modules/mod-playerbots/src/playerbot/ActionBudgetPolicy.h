#pragma once

// twow-repo#541 (owner 11.10.2026, relayed by OB-00): budget for expensive bot actions per map thread and world
// tick, "mit einem Schalter mit mehreren Varianten" - AiPlayerbot.Perf.ActionBudget:
//   0 = off (default)
//   1 = at most 3 expensive actions per map thread and world tick
//   2 = at most 1 expensive action per map thread and world tick
//   3 = spread: a bot may start an expensive action only in its own slot of SpreadSlots consecutive world ticks
// A map thread updates one region at a time; with the shared worker pool one thread can take several regions in a
// tick, so "per thread" is a close stand-in for "per region". A deferred action stays in the bot's queue and is
// tried again on its next update - and after MaxDeferrals deferrals in a row it runs regardless, so nothing starves.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::action_budget
{
    constexpr std::uint32_t SpreadSlots = 4;
    constexpr std::uint32_t MaxDeferrals = 4;

    inline std::uint32_t LimitFor(std::uint32_t mode)
    {
        return mode == 1 ? 3u : mode == 2 ? 1u : 0u;
    }

    // Per map thread: expensive actions started in the current world tick.
    struct ThreadBudget
    {
        std::uint32_t tick = 0;
        std::uint32_t used = 0;
        bool started = false;
    };

    inline ThreadBudget& ThreadLocal()
    {
        thread_local ThreadBudget budget;
        return budget;
    }

    // May this bot start an expensive action now? deferralsInARow is the bot's own count of deferred expensive
    // actions since its last one ran; the caller resets it when this returns true.
    inline bool TryTake(std::uint32_t mode, std::uint32_t botGuid, std::uint32_t tickMs, std::uint32_t tickIndex,
        std::uint32_t deferralsInARow, ThreadBudget& budget)
    {
        if (!mode || deferralsInARow >= MaxDeferrals)
            return true;
        if (mode == 3)
            return (botGuid + tickIndex) % SpreadSlots == 0;

        if (!budget.started || budget.tick != tickMs)
        {
            budget.started = true;
            budget.tick = tickMs;
            budget.used = 0;
        }
        if (budget.used >= LimitFor(mode))
            return false;
        ++budget.used;
        return true;
    }

    // Default list of expensive actions (owner list: travel/rpg target choice, long path search, find corpse,
    // gathering loot scan, target scans); AiPlayerbot.Perf.ActionBudgetActions overrides it.
    inline std::vector<std::string> DefaultExpensiveActions()
    {
        return { "choose travel target", "choose group travel target", "request travel target",
                 "request quest travel target", "choose rpg target", "move to travel target", "find corpse",
                 "add gathering loot", "attack anything" };
    }

    inline bool IsExpensive(std::string const& action, std::vector<std::string> const& expensive)
    {
        return std::find(expensive.begin(), expensive.end(), action) != expensive.end();
    }

    enum Count { Executed, Deferred, Forced, Counts };

    inline std::atomic<std::uint64_t>& Counter(Count count)
    {
        static std::atomic<std::uint64_t> counters[Counts] = {};
        return counters[count];
    }
}
