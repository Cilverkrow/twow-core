#pragma once

// Aggregated world-tick statistics for the scaling gate (twow-repo#351).
//
// perf.log only records updates above PerformanceLog.Slow*Update, so it cannot
// give a percentile: the fast ticks are missing. The world loop hands every
// tick's work time to PerformanceMonitor::RecordTick, which summarises one
// interval with this function and writes a single perf.log line.
//
// Header-only and free of server types so t/tick_stats_test.cpp can check it
// without a world.

#include <algorithm>
#include <cstdint>
#include <vector>

// Both counters are always reported: 100 ms is the "slow update" of the D1
// gate (ADR-0031), 200 ms the live PerformanceLog.SlowMapSystemUpdate, so the
// line is comparable with the classic perf.log entries.
constexpr uint32_t TICK_STATS_SLOW_D1_MS = 100;
constexpr uint32_t TICK_STATS_SLOW_PERFLOG_MS = 200;

struct TickStatsSummary
{
    uint32_t count = 0;
    uint32_t p50 = 0;
    uint32_t p95 = 0;
    uint32_t p99 = 0;
    uint32_t max = 0;
    uint32_t over100 = 0;  // ticks strictly above TICK_STATS_SLOW_D1_MS
    uint32_t over200 = 0;  // ticks strictly above TICK_STATS_SLOW_PERFLOG_MS
};

// Nearest-rank percentile: the smallest sample with at least pct percent of
// the samples at or below it. The vector is reordered, not copied.
inline uint32_t TickStatsPercentile(std::vector<uint32_t>& samples, uint32_t pct)
{
    if (samples.empty())
        return 0;
    size_t const n = samples.size();
    size_t rank = (static_cast<size_t>(pct) * n + 99) / 100;  // ceil(pct * n / 100)
    if (rank == 0)
        rank = 1;
    size_t const index = rank - 1;
    std::nth_element(samples.begin(), samples.begin() + index, samples.end());
    return samples[index];
}

inline TickStatsSummary SummarizeTickStats(std::vector<uint32_t>& samples)
{
    TickStatsSummary s;
    s.count = static_cast<uint32_t>(samples.size());
    if (samples.empty())
        return s;
    for (uint32_t v : samples)
    {
        if (v > s.max)
            s.max = v;
        if (v > TICK_STATS_SLOW_D1_MS)
            ++s.over100;
        if (v > TICK_STATS_SLOW_PERFLOG_MS)
            ++s.over200;
    }
    s.p50 = TickStatsPercentile(samples, 50);
    s.p95 = TickStatsPercentile(samples, 95);
    s.p99 = TickStatsPercentile(samples, 99);
    return s;
}
