#pragma once

// twow-repo#541 audit A47 (owner 11.10.2026 "4-6 nach 5:00", relayed by OB-00): the core saves every player every
// PlayerSave.Interval (60 s in our configs) on its map thread - at 1600 bots ~27 full character saves per second.
// Switch AiPlayerbot.BotSave.IntervalSeconds (0 = off, the core's autosave as before): a bot keeps the core's timer
// from firing and saves itself in its own UpdateAI (its own map thread, never from another thread), at most
// AiPlayerbot.BotSave.MaxPerTick saves per map thread and world tick, its first save offset by its guid so the saves
// spread. Logout still saves through the core. A crash loses at most IntervalSeconds of a bot's progress.

#include <atomic>
#include <cstdint>

namespace ai::bot_save
{
    // Seconds into the interval at which this bot's first save falls (spreads bots that log in together).
    inline std::uint32_t FirstSaveOffset(std::uint32_t guid, std::uint32_t intervalSeconds)
    {
        return intervalSeconds ? guid % intervalSeconds : 0;
    }

    inline bool Due(std::uint32_t nowSeconds, std::uint32_t lastSaveSeconds, std::uint32_t intervalSeconds)
    {
        return intervalSeconds && nowSeconds >= lastSaveSeconds && nowSeconds - lastSaveSeconds >= intervalSeconds;
    }

    // While the switch is on, the core's own timer is kept above this so it never fires before the bot's save.
    inline std::uint32_t HoldCoreTimerMs(std::uint32_t intervalSeconds)
    {
        return (intervalSeconds + 60) * 1000;
    }

    // The budget window. Not WorldTimer::tickTime(): this core never calls WorldTimer::tick(), so tickTime() stays 0 and a
    // budget keyed on it never resets - each map thread saved MaxPerTick bots in its whole lifetime (live v46.1, 11.10.).
    constexpr std::uint32_t BudgetWindowMs = 100;

    inline std::uint32_t BudgetWindow(std::uint32_t nowMs)
    {
        return nowMs / BudgetWindowMs;
    }

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

    inline bool TryTake(std::uint32_t tickMs, std::uint32_t maxPerTick, ThreadBudget& budget)
    {
        if (!maxPerTick)
            return true;
        if (!budget.started || budget.tick != tickMs)
        {
            budget.started = true;
            budget.tick = tickMs;
            budget.used = 0;
        }
        if (budget.used >= maxPerTick)
            return false;
        ++budget.used;
        return true;
    }

    enum Count { Saves, Deferred, SaveMicros, Counts };

    inline std::atomic<std::uint64_t>& Counter(Count count)
    {
        static std::atomic<std::uint64_t> counters[Counts] = {};
        return counters[count];
    }
}
