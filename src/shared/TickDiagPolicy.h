#pragma once

// twow-repo#597 (TICK-01, Refs #592 #541): pure parts of the core tick diagnostics. Every user sits
// behind its own PerformanceLog.* switch (default 0): [WorldTickSlow] rate limit, [TerrainCleanup] /
// [TerrainReload] load counters, [StallWatch] event tracker, [LogWait] write counters, [ResultsSlow]
// rate limit, [MapTickRegion] region window. Measurement only, no behaviour change.
//
// Header-only and free of server types (std only) so t/tick_diag_597_test.cpp can check it without a
// world, and so src/shared (Log.cpp, SqlOperations.cpp) can use it. Counters that several threads
// write are relaxed atomics; the windows read by the world thread only have no lock.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace tick_diag
{
// Steady clock in microseconds (same clock as World::WorldTickNowUs).
inline std::uint64_t NowUs()
{
    return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Wall clock in milliseconds since 1970 (the start= of a [StallWatch] line).
inline std::uint64_t EpochMs()
{
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// Raises an atomic maximum (relaxed, any thread).
inline void RaiseMax(std::atomic<std::uint64_t>& max, std::uint64_t value)
{
    std::uint64_t seen = max.load(std::memory_order_relaxed);
    while (value > seen && !max.compare_exchange_weak(seen, value, std::memory_order_relaxed))
    {
    }
}

// At most `limit` lines per 60 s window; the rest is counted and handed to the next line that is
// written (dropped=). One owner or an outside lock: not thread-safe by itself.
struct RateLimiter
{
    explicit RateLimiter(std::uint32_t linesPerMinute) : limit(linesPerMinute) {}

    std::uint32_t limit;
    std::uint64_t windowStartMs = 0;
    bool started = false;
    std::uint32_t used = 0;
    std::uint32_t dropped = 0;

    bool Allow(std::uint64_t nowMs)
    {
        if (!started || nowMs < windowStartMs || nowMs - windowStartMs >= 60000)
        {
            started = true;
            windowStartMs = nowMs;
            used = 0;
        }
        if (used >= limit)
        {
            ++dropped;
            return false;
        }
        ++used;
        return true;
    }

    // Lines dropped since the last written line; resets the count.
    std::uint32_t TakeDropped()
    {
        std::uint32_t const d = dropped;
        dropped = 0;
        return d;
    }
};

// ---- [LogWait] (PerformanceLog.LogWriteTrace) ----
// Console (stdout/stderr) and file write time of every log line, from any thread.
struct LogWaitSnapshot
{
    std::uint64_t lines = 0;
    std::uint64_t stdoutMaxUs = 0;
    std::uint64_t fileMaxUs = 0;
    std::uint64_t over10ms = 0;    // lines whose console + file write took >= 10 ms
    std::uint64_t over100ms = 0;   // ... >= 100 ms
};

struct LogWaitCounters
{
    std::atomic<std::uint64_t> lines{0};
    std::atomic<std::uint64_t> stdoutMaxUs{0};
    std::atomic<std::uint64_t> fileMaxUs{0};
    std::atomic<std::uint64_t> over10ms{0};
    std::atomic<std::uint64_t> over100ms{0};

    void Record(std::uint64_t stdoutUs, std::uint64_t fileUs)
    {
        lines.fetch_add(1, std::memory_order_relaxed);
        RaiseMax(stdoutMaxUs, stdoutUs);
        RaiseMax(fileMaxUs, fileUs);
        std::uint64_t const total = stdoutUs + fileUs;
        if (total >= 10000)
            over10ms.fetch_add(1, std::memory_order_relaxed);
        if (total >= 100000)
            over100ms.fetch_add(1, std::memory_order_relaxed);
    }

    // Reads and resets (the reader writes the minute line afterwards, so that line only counts
    // into the next window: no recursion).
    LogWaitSnapshot Take()
    {
        LogWaitSnapshot s;
        s.lines = lines.exchange(0, std::memory_order_relaxed);
        s.stdoutMaxUs = stdoutMaxUs.exchange(0, std::memory_order_relaxed);
        s.fileMaxUs = fileMaxUs.exchange(0, std::memory_order_relaxed);
        s.over10ms = over10ms.exchange(0, std::memory_order_relaxed);
        s.over100ms = over100ms.exchange(0, std::memory_order_relaxed);
        return s;
    }
};

// ---- [TerrainReload] (PerformanceLog.TerrainTrace) ----
// GridMap loads of one terrain (TerrainInfo::LoadMapAndVMap), from any thread.
struct TerrainLoadSnapshot
{
    std::uint64_t loads = 0;
    std::uint64_t lazy = 0;          // loaded through GetGrid, i.e. without RefGrid
    std::uint64_t usSum = 0;
    std::uint64_t maxUs = 0;
    std::uint64_t worldThread = 0;
    std::uint64_t otherThread = 0;
};

struct TerrainLoadCounters
{
    std::atomic<std::uint64_t> loads{0};
    std::atomic<std::uint64_t> lazy{0};
    std::atomic<std::uint64_t> usSum{0};
    std::atomic<std::uint64_t> maxUs{0};
    std::atomic<std::uint64_t> worldThread{0};
    std::atomic<std::uint64_t> otherThread{0};

    void Record(std::uint64_t us, bool isLazy, bool onWorldThread)
    {
        loads.fetch_add(1, std::memory_order_relaxed);
        if (isLazy)
            lazy.fetch_add(1, std::memory_order_relaxed);
        usSum.fetch_add(us, std::memory_order_relaxed);
        RaiseMax(maxUs, us);
        (onWorldThread ? worldThread : otherThread).fetch_add(1, std::memory_order_relaxed);
    }

    TerrainLoadSnapshot Take()
    {
        TerrainLoadSnapshot s;
        s.loads = loads.exchange(0, std::memory_order_relaxed);
        s.lazy = lazy.exchange(0, std::memory_order_relaxed);
        s.usSum = usSum.exchange(0, std::memory_order_relaxed);
        s.maxUs = maxUs.exchange(0, std::memory_order_relaxed);
        s.worldThread = worldThread.exchange(0, std::memory_order_relaxed);
        s.otherThread = otherThread.exchange(0, std::memory_order_relaxed);
        return s;
    }
};

// ---- [StallWatch] (PerformanceLog.StallWatchMs) ----
// The watchdog thread steps every 10 ms with the oversleep of that sleep and the age of the
// running world tick. One event = the time either value stays at or above the threshold. A line is
// written when the event opens, at most once a second while it keeps growing, and once when it
// closes with a peak not yet written; on top of that at most linesPerMinute lines (dropped= counts
// the rest).
struct StallLine
{
    std::uint64_t startEpochMs = 0;
    std::uint32_t oversleepMs = 0;   // peak of the event
    std::uint32_t ageMs = 0;         // peak of the event
    std::uint32_t part = 0;          // world tick part at the peak age (or at the open)
    std::uint32_t dropped = 0;
};

class StallTracker
{
public:
    explicit StallTracker(std::uint32_t thresholdMs, std::uint32_t linesPerMinute = 120)
        : m_threshold(thresholdMs), m_limiter(linesPerMinute) {}

    bool Step(std::uint64_t nowEpochMs, std::uint64_t nowSteadyMs, std::uint32_t oversleepMs, std::uint32_t ageMs,
        std::uint32_t part, StallLine& out)
    {
        bool const over = m_threshold && (oversleepMs >= m_threshold || ageMs >= m_threshold);
        if (!over)
        {
            if (!m_open)
                return false;
            m_open = false;
            if (!m_grown)
                return false;
            return Emit(nowSteadyMs, out);
        }
        if (!m_open)
        {
            m_open = true;
            m_grown = false;
            std::uint32_t const back = (std::max)(oversleepMs, ageMs);
            m_start = nowEpochMs > back ? nowEpochMs - back : 0;
            m_peakOversleep = oversleepMs;
            m_peakAge = ageMs;
            m_peakPart = part;
            return Emit(nowSteadyMs, out);
        }
        if (oversleepMs > m_peakOversleep)
        {
            m_peakOversleep = oversleepMs;
            m_grown = true;
        }
        if (ageMs > m_peakAge)
        {
            m_peakAge = ageMs;
            m_peakPart = part;
            m_grown = true;
        }
        if (m_grown && nowSteadyMs - m_lastEmitMs >= 1000)
            return Emit(nowSteadyMs, out);
        return false;
    }

    bool Open() const { return m_open; }

private:
    bool Emit(std::uint64_t nowSteadyMs, StallLine& out)
    {
        m_lastEmitMs = nowSteadyMs;
        m_grown = false;
        if (!m_limiter.Allow(nowSteadyMs))
            return false;
        out.startEpochMs = m_start;
        out.oversleepMs = m_peakOversleep;
        out.ageMs = m_peakAge;
        out.part = m_peakPart;
        out.dropped = m_limiter.TakeDropped();
        return true;
    }

    std::uint32_t m_threshold;
    RateLimiter m_limiter;
    bool m_open = false;
    bool m_grown = false;
    std::uint64_t m_start = 0;
    std::uint64_t m_lastEmitMs = 0;
    std::uint32_t m_peakOversleep = 0;
    std::uint32_t m_peakAge = 0;
    std::uint32_t m_peakPart = 0;
};

// Formats the line into buf (no allocation: the watchdog must not depend on the allocator).
// Returns the length written (0 on error), the line ends with '\n'.
inline std::size_t FormatStallLine(char* buf, std::size_t size, StallLine const& line, char const* partName)
{
    int const n = std::snprintf(buf, size, "[StallWatch] start=%llu oversleep_ms=%u world_tick_age_ms=%u phase=%s dropped=%u\n",
        (unsigned long long)line.startEpochMs, line.oversleepMs, line.ageMs, partName ? partName : "?", line.dropped);
    if (n <= 0)
        return 0;
    return std::size_t(n) < size ? std::size_t(n) : size - 1;
}

// ---- [ResultsSlow] (PerformanceLog.SlowResultCallbackMs) ----
// The callback's type name with blanks made '_' and cut at size-1, so cb= stays one token.
inline void CallbackToken(char* buf, std::size_t size, char const* typeName)
{
    if (!size)
        return;
    std::size_t i = 0;
    if (typeName)
        for (; typeName[i] && i + 1 < size; ++i)
            buf[i] = (typeName[i] == ' ' || typeName[i] == '\t') ? '_' : typeName[i];
    if (!i && size > 1)
        buf[i++] = '-';
    buf[i] = '\0';
}

// ---- [MapTickRegion] (PerformanceLog.MapTickRegion) ----
// One continent region's update in one tick, written by its region thread into its Map, read by the
// world thread after continents.wait().
struct RegionTickSample
{
    bool valid = false;
    std::uint32_t preUs = 0;      // own work before the barrier (Map::Update start -> MarkContinentUpdateFinished)
    std::uint32_t postUs = 0;     // barrier wait incl. extra player updates
    std::uint32_t loops = 0;      // extra UpdatePlayers rounds at the barrier
    std::uint64_t arriveUs = 0;   // steady clock at the barrier
    std::uint64_t exitUs = 0;     // steady clock when the region left the barrier
};

inline std::uint32_t ClampUs(std::uint64_t us)
{
    return us > 0xFFFFFFFFull ? 0xFFFFFFFFu : std::uint32_t(us);
}

// Percentile of the samples (nearest rank, the vector is reordered).
inline std::uint32_t PercentileUs(std::vector<std::uint32_t>& samples, std::uint32_t pct)
{
    if (samples.empty())
        return 0;
    std::size_t rank = (samples.size() * pct + 99) / 100;
    if (rank == 0)
        rank = 1;
    std::nth_element(samples.begin(), samples.begin() + (rank - 1), samples.end());
    return samples[rank - 1];
}

struct RegionStat
{
    std::uint32_t avgUs = 0;
    std::uint32_t p95Us = 0;
    std::uint32_t maxUs = 0;
};

// Bounded: at most MaxRegions regions and SamplesKept samples per region and minute (enough for
// 20 ticks/s; beyond it the average and the maximum stay exact, the p95 uses the first samples).
struct RegionWindow
{
    static constexpr std::size_t MaxRegions = 64;
    static constexpr std::size_t SamplesKept = 1500;

    struct Region
    {
        std::vector<std::uint32_t> pre;
        std::vector<std::uint32_t> post;
        std::uint64_t ticks = 0;
        std::uint64_t preSum = 0;
        std::uint64_t postSum = 0;
        std::uint32_t preMax = 0;
        std::uint32_t postMax = 0;
        std::uint64_t extraLoops = 0;
        std::uint32_t overrunMaxUs = 0;   // time left at the barrier after the last region arrived
        std::uint32_t heldBarrier = 0;    // ticks in which this region arrived last

        RegionStat Pre() { return Stat(pre, preSum, preMax); }
        RegionStat Post() { return Stat(post, postSum, postMax); }

    private:
        RegionStat Stat(std::vector<std::uint32_t>& samples, std::uint64_t sum, std::uint32_t max) const
        {
            RegionStat s;
            if (!ticks)
                return s;
            s.avgUs = ClampUs(sum / ticks);
            s.p95Us = PercentileUs(samples, 95);
            s.maxUs = max;
            return s;
        }
    };

    std::map<std::uint64_t, Region> regions;   // (mapId << 32 | instanceId)
    std::uint64_t overflow = 0;                  // samples of regions beyond MaxRegions (ignored)

    static std::uint64_t Key(std::uint32_t mapId, std::uint32_t instanceId) { return (std::uint64_t(mapId) << 32) | instanceId; }

    void AddTick(std::vector<std::pair<std::uint64_t, RegionTickSample>> const& tick)
    {
        std::uint64_t lastArrive = 0;
        std::uint64_t lastKey = 0;
        bool any = false;
        for (auto const& [key, s] : tick)
        {
            if (!s.valid)
                continue;
            if (!any || s.arriveUs > lastArrive)
            {
                lastArrive = s.arriveUs;
                lastKey = key;
            }
            any = true;
        }
        if (!any)
            return;
        for (auto const& [key, s] : tick)
        {
            if (!s.valid)
                continue;
            auto it = regions.find(key);
            if (it == regions.end())
            {
                if (regions.size() >= MaxRegions)
                {
                    ++overflow;
                    continue;
                }
                it = regions.emplace(key, Region()).first;
            }
            Region& r = it->second;
            ++r.ticks;
            r.preSum += s.preUs;
            r.postSum += s.postUs;
            r.preMax = (std::max)(r.preMax, s.preUs);
            r.postMax = (std::max)(r.postMax, s.postUs);
            if (r.pre.size() < SamplesKept)
            {
                r.pre.push_back(s.preUs);
                r.post.push_back(s.postUs);
            }
            r.extraLoops += s.loops;
            if (s.exitUs > lastArrive)
                r.overrunMaxUs = (std::max)(r.overrunMaxUs, ClampUs(s.exitUs - lastArrive));
            if (key == lastKey)
                ++r.heldBarrier;
        }
    }

    bool Empty() const { return regions.empty() && !overflow; }

    void Clear()
    {
        regions.clear();
        overflow = 0;
    }
};
}
