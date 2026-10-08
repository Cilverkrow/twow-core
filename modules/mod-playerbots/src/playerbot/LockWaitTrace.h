#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace ai::lock_wait
{
// twow-repo#541 (deep dive part 3, OB-00 go 08.10.2026): how long the region threads wait on shared
// locks. Behind AiPlayerbot.LockWaitTrace (default 0): a TimedLock takes the lock with try_lock first
// and only times the wait when that fails, so an uncontended lock costs nothing extra; with the switch
// off it is a plain lock. RandomPlayerbotMgr writes one [LockWait] line per minute.
enum Site : std::uint8_t
{
    AreaLevel,    // TravelMgr::areaLevelMutex (GetAreaLevel's locked path)
    BotLogMutex,  // BotLog's global mutex
    SiteCount
};

struct Counter
{
    std::atomic<std::uint64_t> waits{0};
    std::atomic<std::uint64_t> waitUs{0};
    std::atomic<std::uint64_t> maxUs{0};

    void Add(std::uint64_t us)
    {
        waits.fetch_add(1, std::memory_order_relaxed);
        waitUs.fetch_add(us, std::memory_order_relaxed);
        std::uint64_t seen = maxUs.load(std::memory_order_relaxed);
        while (us > seen && !maxUs.compare_exchange_weak(seen, us, std::memory_order_relaxed))
        {
        }
    }
};

struct Snapshot
{
    std::uint64_t waits = 0;
    std::uint64_t waitUs = 0;
    std::uint64_t maxUs = 0;
};

inline Snapshot Take(Counter& counter)
{
    Snapshot s;
    s.waits = counter.waits.exchange(0, std::memory_order_relaxed);
    s.waitUs = counter.waitUs.exchange(0, std::memory_order_relaxed);
    s.maxUs = counter.maxUs.exchange(0, std::memory_order_relaxed);
    return s;
}

inline std::atomic<bool>& Enabled()
{
    static std::atomic<bool> enabled{false};
    return enabled;
}

inline Counter& Get(Site site)
{
    static Counter counters[SiteCount];
    return counters[site < SiteCount ? site : 0];
}

// Travel searches started (std::async threads, FutureDestinations).
inline std::atomic<std::uint64_t>& AsyncStarts()
{
    static std::atomic<std::uint64_t> starts{0};
    return starts;
}

template <class Mutex>
class TimedLock
{
public:
    TimedLock(Mutex& mutex, Site site) : mutex(mutex)
    {
        if (!Enabled().load(std::memory_order_relaxed))
        {
            mutex.lock();
            return;
        }
        if (mutex.try_lock())
            return;
        auto const start = std::chrono::steady_clock::now();
        mutex.lock();
        Get(site).Add(std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count()));
    }

    ~TimedLock() { mutex.unlock(); }

    TimedLock(TimedLock const&) = delete;
    TimedLock& operator=(TimedLock const&) = delete;

private:
    Mutex& mutex;
};
}
