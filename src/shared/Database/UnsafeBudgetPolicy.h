#pragma once

// twow-repo#597 (TICK-01, Refs #592 #541): pure parts of AsyncQueries.UnsafeBudgetMs, the time budget of
// the thread-unsafe async query callbacks (player and bot logins: LoadFromDB + Map::Add + grid load) that
// SqlResultQueue::Update runs on the world thread. 0 (default) = no budget, today's loop.
//
// Header-only and free of server types (std only) so t/unsafe_budget_597_test.cpp can check it without a
// world. The counters are relaxed atomics: today only the world thread runs SqlResultQueue::Update, the
// atomics keep it safe if another thread ever does.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace unsafe_budget
{
// Decided BEFORE the next thread-unsafe callback is taken off the queue:
// - budgetMs 0 = never stop (today's behaviour);
// - ranThisPass 0 = go: the first callback of a pass always runs (progress guarantee, at least one per call);
// - otherwise stop once this pass's unsafe loop has used at least budgetMs.
// A stop leaves that callback at the front of the queue, so the remaining ones run on the next tick(s) in
// the same order.
inline bool StopBefore(std::uint32_t budgetMs, std::uint32_t ranThisPass, std::uint64_t elapsedUs)
{
    return budgetMs && ranThisPass && elapsedUs >= std::uint64_t(budgetMs) * 1000;
}

// ---- [UnsafeBudget] minute line (only while AsyncQueries.UnsafeBudgetMs > 0) ----
struct Snapshot
{
    std::uint64_t callsCapped = 0;   // passes the budget stopped with callbacks left in the queue
    std::uint64_t deferredMax = 0;   // most unsafe callbacks left queued by one stop
    std::uint64_t ran = 0;           // unsafe callbacks run (all passes)
    std::uint64_t loopMaxUs = 0;     // longest unsafe loop of one pass
};

struct Counters
{
    std::atomic<std::uint64_t> callsCapped{0};
    std::atomic<std::uint64_t> deferredMax{0};
    std::atomic<std::uint64_t> ran{0};
    std::atomic<std::uint64_t> loopMaxUs{0};

    // One pass of the unsafe loop that ran at least one callback. capped = the budget stopped it,
    // deferred = unsafe callbacks still queued after the stop.
    void Record(std::uint32_t ranThisPass, bool capped, std::uint32_t deferred, std::uint64_t loopUs)
    {
        ran.fetch_add(ranThisPass, std::memory_order_relaxed);
        RaiseMax(loopMaxUs, loopUs);
        if (capped)
        {
            callsCapped.fetch_add(1, std::memory_order_relaxed);
            RaiseMax(deferredMax, deferred);
        }
    }

    // Reads and resets (the minute line covers the window since the last Take).
    Snapshot Take()
    {
        Snapshot s;
        s.callsCapped = callsCapped.exchange(0, std::memory_order_relaxed);
        s.deferredMax = deferredMax.exchange(0, std::memory_order_relaxed);
        s.ran = ran.exchange(0, std::memory_order_relaxed);
        s.loopMaxUs = loopMaxUs.exchange(0, std::memory_order_relaxed);
        return s;
    }

private:
    static void RaiseMax(std::atomic<std::uint64_t>& max, std::uint64_t value)
    {
        std::uint64_t seen = max.load(std::memory_order_relaxed);
        while (value > seen && !max.compare_exchange_weak(seen, value, std::memory_order_relaxed))
        {
        }
    }
};

// Formats the minute line into buf (no trailing newline). Returns the length written (0 on error).
inline std::size_t FormatLine(char* buf, std::size_t size, Snapshot const& s)
{
    int const n = std::snprintf(buf, size, "[UnsafeBudget] calls_capped=%llu deferred_max=%llu ran=%llu loop_max_ms=%.1f",
        (unsigned long long)s.callsCapped, (unsigned long long)s.deferredMax, (unsigned long long)s.ran,
        s.loopMaxUs / 1000.0);
    if (n <= 0)
        return 0;
    return std::size_t(n) < size ? std::size_t(n) : size - 1;
}
}
