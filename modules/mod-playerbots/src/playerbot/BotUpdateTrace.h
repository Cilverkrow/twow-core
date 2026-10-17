#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ai::bot_update
{
// twow-repo#541 (deep dive: spikes per region update, owner approval for measurement counters):
// wall time of one PlayerbotAI::UpdateAI call, over all bots and map threads. Behind
// AiPlayerbot.BotUpdateTrace (default 0). Log2 buckets in microseconds give p50/p90/p99/p999 per
// minute; the slowest calls keep who and what (bot, map, combat, last action), so the spike
// sources show up by name. With the switch off nothing is timed.
constexpr std::size_t Buckets = 24;         // bucket b holds [2^b, 2^(b+1)) us; bucket 0 also 0 us
constexpr std::size_t SlowestKept = 5;

// Cost attribution inside one update (OB-00 go 10.10.2026, same switch): the engine trace gets the
// microseconds of each evaluated action and of the value-update + trigger phase.
inline std::uint64_t NowUs()
{
    return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline std::uint64_t SinceUs(std::uint64_t startUs)
{
    std::uint64_t const now = NowUs();
    return now > startUs ? now - startUs : 0;
}

// twow-repo#597 (TICK-01, AiPlayerbot.BotUpdateTraceTick): appended at the END of the [BotUpdateSlow]
// description, i.e. at the end of the line (OB-10 appends its own field after region= later), so a slow
// bot update can be matched with [WorldTickSlow] tick= and [MapTickRegion] region=.
inline std::string TickRegionSuffix(std::uint32_t worldLoop, std::uint32_t mapId, std::uint32_t instanceId)
{
    return " tick=" + std::to_string(worldLoop) + " region=" + std::to_string(mapId) + "." + std::to_string(instanceId);
}

inline std::size_t BucketOf(std::uint64_t us)
{
    std::size_t b = 0;
    while (us > 1 && b + 1 < Buckets)
    {
        us >>= 1;
        ++b;
    }
    return b;
}

// Upper edge of a bucket, in microseconds (the reported percentile is "at most this").
inline std::uint64_t BucketUpperUs(std::size_t b)
{
    return (std::uint64_t(1) << (b + 1)) - 1;
}

struct Slow
{
    std::uint64_t us = 0;
    std::string who;    // "name map=M combat=C action=A"
};

struct Snapshot
{
    std::uint64_t calls = 0;
    std::uint64_t totalUs = 0;
    std::uint64_t maxUs = 0;
    std::array<std::uint64_t, Buckets> buckets{};
    std::vector<Slow> slowest;   // longest first
};

// Smallest bucket edge at or above the given share of calls (0 < share <= 1).
inline std::uint64_t PercentileUs(Snapshot const& s, double share)
{
    if (!s.calls)
        return 0;
    std::uint64_t const target = std::uint64_t(double(s.calls) * share + 0.999999);
    std::uint64_t seen = 0;
    for (std::size_t b = 0; b < Buckets; ++b)
    {
        seen += s.buckets[b];
        if (seen >= target)
            return BucketUpperUs(b);
    }
    return BucketUpperUs(Buckets - 1);
}

// Calls in buckets that lie entirely at or above the threshold (a lower bound of the true count).
inline std::uint64_t CallsAtLeastUs(Snapshot const& s, std::uint64_t thresholdUs)
{
    std::uint64_t count = 0;
    for (std::size_t b = 0; b < Buckets; ++b)
        if ((std::uint64_t(1) << b) >= thresholdUs)
            count += s.buckets[b];
    return count;
}

class Recorder
{
public:
    // Any thread. The slow list takes a lock only for calls longer than the current threshold.
    template <class Describe>
    void Add(std::uint64_t us, Describe describe)
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        totalUs.fetch_add(us, std::memory_order_relaxed);
        buckets[BucketOf(us)].fetch_add(1, std::memory_order_relaxed);
        std::uint64_t seen = maxUs.load(std::memory_order_relaxed);
        while (us > seen && !maxUs.compare_exchange_weak(seen, us, std::memory_order_relaxed))
        {
        }
        if (us <= slowThreshold.load(std::memory_order_relaxed))
            return;

        std::lock_guard<std::mutex> lock(slowMutex);
        slow.push_back({ us, describe() });
        std::sort(slow.begin(), slow.end(), [](Slow const& a, Slow const& b) { return a.us > b.us; });
        if (slow.size() > SlowestKept)
            slow.resize(SlowestKept);
        if (slow.size() == SlowestKept)
            slowThreshold.store(slow.back().us, std::memory_order_relaxed);
    }

    // Reads and resets everything (the reader is the per-minute line).
    Snapshot Take()
    {
        Snapshot s;
        s.calls = calls.exchange(0, std::memory_order_relaxed);
        s.totalUs = totalUs.exchange(0, std::memory_order_relaxed);
        s.maxUs = maxUs.exchange(0, std::memory_order_relaxed);
        for (std::size_t b = 0; b < Buckets; ++b)
            s.buckets[b] = buckets[b].exchange(0, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(slowMutex);
        s.slowest.swap(slow);
        slowThreshold.store(0, std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> totalUs{0};
    std::atomic<std::uint64_t> maxUs{0};
    std::array<std::atomic<std::uint64_t>, Buckets> buckets{};
    std::atomic<std::uint64_t> slowThreshold{0};
    std::mutex slowMutex;
    std::vector<Slow> slow;
};

inline Recorder& Global()
{
    static Recorder recorder;
    return recorder;
}

// twow-repo#601 S1 (OB-10 18.10.2026, same switch): the cost of one PreValidateTurnIns call with the work it had
// (open turn-ins checked) and its result (turn-ins validated) - the [PreValidate] minute line covers every call, the
// trail note "TIME:prevalidate" only the ones a [BotUpdateSlow] line shows. Atomics only, no slow list, no lock.
struct CountedSnapshot
{
    Snapshot time;              // calls, totalUs, maxUs, buckets (slowest stays empty)
    std::uint64_t openSum = 0;  // open turn-ins over all calls
    std::uint64_t okSum = 0;    // validated turn-ins over all calls
};

class CountedRecorder
{
public:
    // Any thread.
    void Add(std::uint64_t us, std::uint32_t open, std::uint32_t ok)
    {
        totalUs.fetch_add(us, std::memory_order_relaxed);
        buckets[BucketOf(us)].fetch_add(1, std::memory_order_relaxed);
        openSum.fetch_add(open, std::memory_order_relaxed);
        okSum.fetch_add(ok, std::memory_order_relaxed);
        std::uint64_t seen = maxUs.load(std::memory_order_relaxed);
        while (us > seen && !maxUs.compare_exchange_weak(seen, us, std::memory_order_relaxed))
        {
        }
    }

    // Reads and resets everything (the reader is the per-minute line). calls is the sum of the buckets taken: an Add
    // between the exchanges then lands in one window or the next, never as a call without its bucket - with few calls
    // a missing bucket would turn p99 into the top bucket edge.
    CountedSnapshot Take()
    {
        CountedSnapshot s;
        s.time.totalUs = totalUs.exchange(0, std::memory_order_relaxed);
        s.time.maxUs = maxUs.exchange(0, std::memory_order_relaxed);
        for (std::size_t b = 0; b < Buckets; ++b)
        {
            s.time.buckets[b] = buckets[b].exchange(0, std::memory_order_relaxed);
            s.time.calls += s.time.buckets[b];
        }
        s.openSum = openSum.exchange(0, std::memory_order_relaxed);
        s.okSum = okSum.exchange(0, std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<std::uint64_t> totalUs{0};
    std::atomic<std::uint64_t> maxUs{0};
    std::array<std::atomic<std::uint64_t>, Buckets> buckets{};
    std::atomic<std::uint64_t> openSum{0};
    std::atomic<std::uint64_t> okSum{0};
};

inline CountedRecorder& Prevalidate()
{
    static CountedRecorder recorder;
    return recorder;
}
}
