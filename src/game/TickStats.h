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
#include <cstdio>
#include <vector>

// Both counters are always reported: 100 ms is the "slow update" of the D1
// gate (ADR-0031), 200 ms the live PerformanceLog.SlowMapSystemUpdate, so the
// line is comparable with the classic perf.log entries.
constexpr uint32_t TICK_STATS_SLOW_D1_MS = 100;
constexpr uint32_t TICK_STATS_SLOW_PERFLOG_MS = 200;

// twow-repo#592 (PATH-02, PerformanceLog.TickStatsExtended): the live-near loads
// sit between 20 and 100 ms (roster 540: p95 30-40 ms, over100 1.7/min), where
// over100 does not move. Counted with the others, printed only with the switch.
// Not the same edges as [BotUpdate] over10ms/over20ms (log2 buckets, >= 16.4 /
// 32.8 ms): these are strictly above 20 and 50 ms.
constexpr uint32_t TICK_STATS_OVER20_MS = 20;
constexpr uint32_t TICK_STATS_OVER50_MS = 50;

struct TickStatsSummary
{
    uint32_t count = 0;
    uint32_t p50 = 0;
    uint32_t p95 = 0;
    uint32_t p99 = 0;
    uint32_t max = 0;
    uint32_t over100 = 0;  // ticks strictly above TICK_STATS_SLOW_D1_MS
    uint32_t over200 = 0;  // ticks strictly above TICK_STATS_SLOW_PERFLOG_MS
    uint32_t over20 = 0;   // ticks strictly above TICK_STATS_OVER20_MS (#592)
    uint32_t over50 = 0;   // ticks strictly above TICK_STATS_OVER50_MS (#592)
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
        if (v > TICK_STATS_OVER20_MS)
            ++s.over20;
        if (v > TICK_STATS_OVER50_MS)
            ++s.over50;
    }
    s.p50 = TickStatsPercentile(samples, 50);
    s.p95 = TickStatsPercentile(samples, 95);
    s.p99 = TickStatsPercentile(samples, 99);
    return s;
}

// The "Tick stats" perf.log line. extended = false gives exactly the #351 line;
// extended = true appends " over20=N over50=N" behind over200 (parsers that read
// key=value or match "over100=" keep working). Returns the snprintf result.
inline int FormatTickStatsLine(char* out, size_t size, uint32_t intervalSeconds, TickStatsSummary const& s, bool extended)
{
    if (!extended)
        return std::snprintf(out, size, "Tick stats: interval=%us ticks=%u p50=%ums p95=%ums p99=%ums max=%ums over100=%u over200=%u",
            intervalSeconds, s.count, s.p50, s.p95, s.p99, s.max, s.over100, s.over200);
    return std::snprintf(out, size, "Tick stats: interval=%us ticks=%u p50=%ums p95=%ums p99=%ums max=%ums over100=%u over200=%u over20=%u over50=%u",
        intervalSeconds, s.count, s.p50, s.p95, s.p99, s.max, s.over100, s.over200, s.over20, s.over50);
}
