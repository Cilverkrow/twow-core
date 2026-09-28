#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ai::travel_choose
{
// twow-repo#416 (train 7.2, 2026-09-28): map stalls of 5 s (Rollotheo, first
// update after login) and 21 s (04:06, Stalen, 4 s after a revive) sit in one
// bot update while the bot has no travel target yet. Two unbounded pieces of
// work on the map thread:
// - The destination lists are computed by std::async. A std::future from
//   std::async blocks in its destructor until the job is done, so replacing or
//   clearing a pending one (a reset, a new request) waits for the whole job.
// - SetBestTarget checks candidate after candidate (IsActive, area level,
//   danger map) with no limit per update.

// Time one choice may use per update. Ticks run at p50 ~40 ms / p95 ~190 ms;
// 250 ms keeps a single bot below the 1000 ms [BotSlowUpdate] mark and far
// below the 3000 ms stall line, and still covers hundreds of cheap checks.
constexpr uint32_t BudgetMs = 250;
constexpr uint32_t LogSeconds = 60;

// At most this many parked jobs. Each bot runs one job at a time, so the async
// threads stay below bots + ParkCap; while the lot is full no new job starts
// (the request is retried next update) - nobody waits. 32 covers a login wave
// or a mass revive; normal play parks a handful.
constexpr size_t ParkCap = 32;
// Finished parked jobs are collected from the bot updates, at most this often.
constexpr uint32_t CollectIntervalMs = 100;

inline bool OverBudget(uint32_t elapsedMs)
{
    return elapsedMs > BudgetMs;
}

// A choice that ran out of time goes on with the next list where it stopped:
// the candidates already checked are skipped. Another purpose starts afresh.
struct Resume
{
    std::string purpose;
    uint32_t skip = 0;

    uint32_t SkipFor(std::string const& forPurpose) const
    {
        return forPurpose == purpose ? skip : 0;
    }

    void Abort(std::string const& forPurpose, uint32_t checkedUpTo)
    {
        purpose = forPurpose;
        skip = checkedUpTo;
    }

    void Clear()
    {
        purpose.clear();
        skip = 0;
    }
};

// Keeps jobs that are still running, so no map thread waits for them. A
// finished job is simply dropped - destroying it does not block. The jobs
// capture their inputs by value, so they may outlive the bot.
template <class Future>
class ParkingLot
{
public:
    // Returns the number of jobs parked after this call.
    size_t Park(Future&& future)
    {
        if (!future.valid() || future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            return Pending();

        std::lock_guard<std::mutex> lock(mutex);
        DropFinished();
        parked.push_back(std::move(future));
        Publish();
        return parked.size();
    }

    // Drops finished jobs; returns the number still running.
    size_t Pending()
    {
        std::lock_guard<std::mutex> lock(mutex);
        DropFinished();
        Publish();
        return parked.size();
    }

    // Lock-free reads for the hot path.
    size_t Count() const { return count.load(); }
    size_t Max() const { return max.load(); }
    bool Full() const { return count.load() >= ParkCap; }

private:
    void DropFinished()
    {
        parked.erase(std::remove_if(parked.begin(), parked.end(), [](Future& f)
            {
                return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            }), parked.end());
    }

    void Publish()
    {
        count.store(parked.size());
        if (parked.size() > max.load())
            max.store(parked.size());
    }

    std::mutex mutex;
    std::vector<Future> parked;
    std::atomic<size_t> count{ 0 };
    std::atomic<size_t> max{ 0 };
};
}
