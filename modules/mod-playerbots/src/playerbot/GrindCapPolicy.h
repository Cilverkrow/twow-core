#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace ai::grind_cap
{
// #307 (D3 root cause, group C): the grind target choice accepts mobs up to four
// levels above the bot. For a low-level roster bot that is a pack it cannot
// survive: a level 3 goblin attacked level 6 Mudpaw Miners 123 times and died
// 136 times in two trains. Below lowLevelBelow a roster bot on its own accepts
// at most lowLevelMargin levels above itself; 0 disables the rule.
inline int MaxLevelsAbove(bool rosterOnItsOwn, std::uint32_t botLevel, std::uint32_t lowLevelBelow,
    int lowLevelMargin, int defaultMargin = 4)
{
    if (rosterOnItsOwn && lowLevelBelow > 0 && botLevel < lowLevelBelow)
        return lowLevelMargin < defaultMargin ? lowLevelMargin : defaultMargin;
    return defaultMargin;
}

// Per bot and creature entry: after maxDeaths deaths to the same entry within
// windowSeconds, the entry is avoided as a grind target for avoidSeconds.
struct Record
{
    std::uint32_t windowStart = 0;
    std::uint32_t deaths = 0;
    std::uint32_t avoidUntil = 0;
};

// Returns true when this death starts an avoidance period.
inline bool RecordDeath(Record& record, std::uint32_t now, std::uint32_t maxDeaths,
    std::uint32_t windowSeconds, std::uint32_t avoidSeconds)
{
    if (maxDeaths == 0)
        return false;
    if (record.deaths == 0 || now >= record.windowStart + windowSeconds)
    {
        record.windowStart = now;
        record.deaths = 0;
    }
    if (++record.deaths < maxDeaths)
        return false;
    record.deaths = 0;
    record.avoidUntil = now + avoidSeconds;
    return true;
}

inline bool IsAvoided(Record const& record, std::uint32_t now)
{
    return record.avoidUntil > now;
}

// Train 6 tick regression: the records were kept as qualified "manual time" /
// "manual int" values in each bot's AI context. Every grind candidate *read*
// created one value per creature entry the bot ever saw, never removed, and the
// engine iterates all created values every tick - the tick cost grew with time.
// One bounded, locked store instead; reads never create anything.
class AvoidStore
{
public:
    static constexpr std::size_t Stripes = 64;
    static constexpr std::size_t MaxRecords = 8192;
    static constexpr std::size_t MaxPerStripe = MaxRecords / Stripes;

    // #351 review: a std::shared_mutex (glibc prefers readers) let a stress
    // test with 8 busy readers starve the writers for minutes. 64 stripes of
    // plain mutexes keep every critical section to one hash lookup, spread
    // the map threads, and cannot starve a death being recorded.
    bool IsAvoided(std::uint32_t bot, std::uint32_t entry, std::uint32_t now) const
    {
        if (!size.load(std::memory_order_relaxed))
            return false;
        std::uint64_t const key = Key(bot, entry);
        Stripe const& stripe = stripes[Index(key)];
        std::lock_guard<std::mutex> lock(stripe.mutex);
        auto const it = stripe.records.find(key);
        return it != stripe.records.end() && grind_cap::IsAvoided(it->second, now);
    }

    // Returns true when this death starts an avoidance period; out receives
    // the updated state.
    bool RecordDeath(std::uint32_t bot, std::uint32_t entry, std::uint32_t now, std::uint32_t maxDeaths,
        std::uint32_t windowSeconds, std::uint32_t avoidSeconds, Record& out)
    {
        std::uint64_t const key = Key(bot, entry);
        Stripe& stripe = stripes[Index(key)];
        std::lock_guard<std::mutex> lock(stripe.mutex);
        std::size_t const before = stripe.records.size();
        if (before >= MaxPerStripe)
        {
            for (auto it = stripe.records.begin(); it != stripe.records.end();)
            {
                bool const stale = it->second.avoidUntil <= now && it->second.windowStart + windowSeconds <= now;
                it = stale ? stripe.records.erase(it) : std::next(it);
            }
            if (stripe.records.size() >= MaxPerStripe)
                stripe.records.clear();
        }
        Record& record = stripe.records[key];
        bool const avoid = grind_cap::RecordDeath(record, now, maxDeaths, windowSeconds, avoidSeconds);
        out = record;
        std::size_t const after = stripe.records.size();
        if (after >= before)
            size.fetch_add(after - before, std::memory_order_relaxed);
        else
            size.fetch_sub(before - after, std::memory_order_relaxed);
        return avoid;
    }

    std::size_t Size() const { return size.load(std::memory_order_relaxed); }

private:
    struct Stripe
    {
        mutable std::mutex mutex;
        std::unordered_map<std::uint64_t, Record> records;
    };

    static std::uint64_t Key(std::uint32_t bot, std::uint32_t entry)
    {
        return (std::uint64_t(bot) << 32) | entry;
    }

    static std::size_t Index(std::uint64_t key)
    {
        return std::size_t(((key ^ (key >> 29)) * 0x9E3779B97F4A7C15ull) >> 58);   // 6 bits = 64 stripes
    }

    std::array<Stripe, Stripes> stripes;
    std::atomic<std::size_t> size{ 0 };
};

inline AvoidStore& Avoids()
{
    static AvoidStore store;
    return store;
}
}
