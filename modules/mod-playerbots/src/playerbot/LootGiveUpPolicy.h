#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ai::loot_giveup
{
// AiPlayerbot.Loot.MoveGiveUp (twow-repo#703 item 3, CLI-PATH design v2 4e9c26b5, OB-10 OK 11.11.2026; cohort mode
// 0 off (default) / 1 all / 2 cohort B / 3 cohort A, other values -> 0).
//
// Move-to-loot loop: "move to loot" follows the bot's loot target without an attempt count. With a target the path finder
// cannot reach ("mmap pts=1, keep last") the bot stood for hours (ARB N1: bots 853 / 169 / 2142, 219-262 min, moved=0.0).
// Path.PosStuck does not see it (it lives in TravelTarget::CheckStatus, which "move to loot" never calls).
//
// With the key a roster bot on its own gives the loot target up when it came no nearer for WindowSeconds of continuous trying
// with at least MinTries tries: "loot target" reset, the object out of the bot's loot list for ExcludeSeconds (every loot
// kind: corpse, herb, ore, chest), one [LootGiveUp] line. Pure rules here, no game types, no locks, no config reads.

constexpr std::int64_t WindowSeconds = 30;     // no progress for this long ...
constexpr std::uint32_t MinTries = 5;          // ... and at least this many tries in that span
constexpr float MinImproveYards = 1.0f;        // progress = the distance improves the best one by at least this
constexpr std::int64_t MaxGapSeconds = 5;      // a longer gap between two tries on the same guid restarts the state
constexpr std::int64_t ExcludeSeconds = 600;   // a given-up object stays out of the bot's loot list this long
constexpr std::size_t MaxBlocked = 16;         // per bot; when full the oldest entry leaves

// The per-bot state of the current approach (the bot's own thread).
struct Try
{
    std::uint64_t guid = 0;
    float distStart = 0.0f;     // the distance at the first try of this approach
    float distBest = 0.0f;      // the best distance so far (moved only by a >= MinImproveYards step)
    std::int64_t begun = 0;     // the first try of this approach (waited_s)
    std::int64_t since = 0;     // the try clock: the first try or the last progress
    std::int64_t last = 0;      // the last try on this guid
    std::uint32_t tries = 0;    // tries since the clock started (the starting try included)
};

enum class Step
{
    Restart,   // no state yet, a new guid, or a gap > MaxGapSeconds since the last try: the state starts again
    Progress,  // >= MinImproveYards nearer than the best so far: the clock starts again
    Keep,      // no progress yet, not long enough
    GiveUp     // no progress for >= WindowSeconds and >= MinTries tries
};

inline Step Next(Try& t, std::uint64_t guid, float dist, std::int64_t now)
{
    if (t.tries == 0 || guid != t.guid || now - t.last > MaxGapSeconds)
    {
        t.guid = guid;
        t.distStart = dist;
        t.distBest = dist;
        t.begun = now;
        t.since = now;
        t.last = now;
        t.tries = 1;
        return Step::Restart;
    }
    t.last = now;
    if (dist <= t.distBest - MinImproveYards)
    {
        t.distBest = dist;
        t.since = now;
        t.tries = 1;
        return Step::Progress;
    }
    ++t.tries;
    if (now - t.since >= WindowSeconds && t.tries >= MinTries)
        return Step::GiveUp;
    return Step::Keep;
}

// The per-bot exclusion list (guid -> until). Filled only by a give-up, so with the key 0 it stays empty. Every change
// returns the change in the number of held entries, for the shared loot_blocked gauge.
class BlockList
{
public:
    // Expired entries leave first; the guid is renewed when held, else added; when full the oldest (smallest until) leaves.
    int Add(std::uint64_t guid, std::int64_t until, std::int64_t now)
    {
        int delta = Purge(now);
        for (std::uint32_t i = 0; i < count; ++i)
            if (entries[i].guid == guid)
            {
                entries[i].until = until;
                return delta;
            }
        if (count < MaxBlocked)
        {
            entries[count++] = Entry{ guid, until };
            return delta + 1;
        }
        std::uint32_t oldest = 0;
        for (std::uint32_t i = 1; i < count; ++i)
            if (entries[i].until < entries[oldest].until)
                oldest = i;
        entries[oldest] = Entry{ guid, until };
        return delta;
    }

    bool Blocks(std::uint64_t guid, std::int64_t now) const
    {
        for (std::uint32_t i = 0; i < count; ++i)
            if (entries[i].guid == guid)
                return now < entries[i].until;
        return false;
    }

    // Drops the entries with now >= until; returns minus the number dropped.
    int Purge(std::int64_t now)
    {
        int dropped = 0;
        for (std::uint32_t i = 0; i < count;)
            if (now >= entries[i].until)
            {
                entries[i] = entries[--count];
                ++dropped;
            }
            else
                ++i;
        return -dropped;
    }

    std::uint32_t Size() const { return count; }

private:
    struct Entry
    {
        std::uint64_t guid;
        std::int64_t until;
    };
    std::array<Entry, MaxBlocked> entries{};
    std::uint32_t count = 0;
};

// [LootGiveUpMin] loot_giveup= loot_blocked=, one line a minute (shared by all map threads). loot_blocked = the entries held
// in the bots' exclusion lists at the minute; an expired entry leaves at its bot's next loot check (LootObjectStack::CanLoot).
struct Counters
{
    std::atomic<std::uint32_t> giveUps{0};
    std::atomic<std::int64_t> held{0};
    std::atomic<std::uint64_t> lastLogMinute{0};

    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t const minute = nowSeconds / 60;
        std::uint64_t seen = lastLogMinute.load(std::memory_order_relaxed);
        return seen != minute && lastLogMinute.compare_exchange_strong(seen, minute, std::memory_order_relaxed);
    }

    std::uint32_t TakeGiveUps() { return giveUps.exchange(0, std::memory_order_relaxed); }

    std::uint32_t Held() const
    {
        std::int64_t const v = held.load(std::memory_order_relaxed);
        return v > 0 ? std::uint32_t(v) : 0u;
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }
}
