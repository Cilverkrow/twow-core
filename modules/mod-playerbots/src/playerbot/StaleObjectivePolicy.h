#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

// twow-repo#601 cand4 (i) (AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter, CLI-Deep 29.10.2026; OB-00 after CLI-PATH's
// far-travel-stuck/as-ab/RESULT-as-1-why.md): in cand4r-B 568 of 716 cohort-B deaths fell on quests no bot made progress on all run.
// The per-bot rules (#307 DestinationDeaths, #581 QL-1) let every bot pay its own deaths and failures at an objective nobody wins.
// This is the realm-wide memory: an objective (quest, objective entry) with Params::failures failures from at least Params::bots
// distinct bots within Params::windowMs, and no credit of its quest since, is marked for Params::markMs; any credit of the quest
// clears its keys. Pure (std only): the unit test and the source contract pin it without game headers. The process-wide store and
// its mutex live in TravelMgr.cpp; nothing here locks.
namespace ai::stale_objective
{
constexpr std::size_t MaxKeys = 2048;       // bounded: when full, stale unmarked keys go first, then the least recently touched
constexpr std::size_t KeptFailures = 8;     // recent failures kept per key (a ring)

struct Params
{
    std::uint32_t failures = 4;
    std::uint32_t bots = 3;
    std::uint32_t windowMs = 3600000;
    std::uint32_t markMs = 3600000;
};

struct Failure
{
    std::uint32_t bot = 0;
    std::uint32_t atMs = 0;
};

struct Entry
{
    Failure recent[KeptFailures] = {};
    std::uint32_t filled = 0;       // slots in use, at most KeptFailures
    std::uint32_t next = 0;         // ring write index
    std::uint32_t markUntilMs = 0;  // 0 = no mark
    std::uint32_t lastTouchMs = 0;
};

// quest, objective entry (creature > 0, game object < 0)
using Key = std::pair<std::uint32_t, std::int32_t>;

struct Store
{
    std::map<Key, Entry> entries;
};

// The world timer wraps after ~49 days: only differences are compared.
inline bool Within(std::uint32_t nowMs, std::uint32_t atMs, std::uint32_t spanMs)
{
    return std::uint32_t(nowMs - atMs) <= spanMs;
}

inline bool IsMarked(Entry const& entry, std::uint32_t nowMs)
{
    return entry.markUntilMs != 0 && std::int32_t(entry.markUntilMs - nowMs) > 0;
}

// The failures inside the window and how many distinct bots they came from.
inline void CountWindow(Entry const& entry, std::uint32_t nowMs, std::uint32_t windowMs, std::uint32_t& failures, std::uint32_t& bots)
{
    failures = 0;
    bots = 0;
    std::uint32_t seen[KeptFailures] = {};
    for (std::uint32_t i = 0; i < entry.filled; ++i)
    {
        Failure const& failure = entry.recent[i];
        if (!Within(nowMs, failure.atMs, windowMs))
            continue;
        ++failures;
        bool known = false;
        for (std::uint32_t j = 0; j < bots; ++j)
            known = known || seen[j] == failure.bot;
        if (!known)
            seen[bots++] = failure.bot;
    }
}

// Room for one new key: unmarked keys untouched for a whole window go first, then the least recently touched key.
inline void MakeRoom(Store& store, std::uint32_t nowMs, Params const& params)
{
    if (store.entries.size() < MaxKeys)
        return;
    for (auto it = store.entries.begin(); it != store.entries.end();)
        it = !IsMarked(it->second, nowMs) && !Within(nowMs, it->second.lastTouchMs, params.windowMs) ? store.entries.erase(it) : std::next(it);
    if (store.entries.size() < MaxKeys)
        return;
    auto oldest = store.entries.begin();
    for (auto it = store.entries.begin(); it != store.entries.end(); ++it)
        if (std::uint32_t(nowMs - it->second.lastTouchMs) > std::uint32_t(nowMs - oldest->second.lastTouchMs))
            oldest = it;
    store.entries.erase(oldest);
}

// One failure of bot at key. True when this failure sets a new mark; an existing mark is not extended. failuresOut / botsOut get the
// window counts after the failure (for the mark line).
inline bool RecordFailure(Store& store, Key const& key, std::uint32_t bot, std::uint32_t nowMs, Params const& params,
    std::uint32_t& failuresOut, std::uint32_t& botsOut)
{
    if (store.entries.find(key) == store.entries.end())
        MakeRoom(store, nowMs, params);
    Entry& entry = store.entries[key];
    entry.recent[entry.next] = Failure{ bot, nowMs };
    entry.next = (entry.next + 1) % KeptFailures;
    if (entry.filled < KeptFailures)
        ++entry.filled;
    entry.lastTouchMs = nowMs;
    CountWindow(entry, nowMs, params.windowMs, failuresOut, botsOut);
    if (IsMarked(entry, nowMs) || failuresOut < params.failures || botsOut < params.bots)
        return false;
    entry.markUntilMs = nowMs + params.markMs;
    if (!entry.markUntilMs)
        entry.markUntilMs = 1;
    return true;
}

// A credit of the quest: its keys are dropped (the failures before the credit no longer count). Returns the marks it ended.
inline std::uint32_t Credit(Store& store, std::uint32_t questId, std::uint32_t nowMs)
{
    std::uint32_t ended = 0;
    auto it = store.entries.lower_bound(Key{ questId, INT32_MIN });
    while (it != store.entries.end() && it->first.first == questId)
    {
        if (IsMarked(it->second, nowMs))
            ++ended;
        it = store.entries.erase(it);
    }
    return ended;
}

inline bool IsStale(Store const& store, Key const& key, std::uint32_t nowMs)
{
    auto const it = store.entries.find(key);
    return it != store.entries.end() && IsMarked(it->second, nowMs);
}
}
