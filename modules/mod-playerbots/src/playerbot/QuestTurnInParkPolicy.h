#pragma once

#include <array>
#include <cstdint>

namespace ai::turnin_park
{
// twow-repo#485 (live v24, 2026-10-02, 180 roster bots, 2.67 h): one finished quest
// limited a roster bot's quest routes to turn-ins (2,529 of 2,740 requests) and held its
// gathering back. With a taker it never reached - another continent, a stall in a city,
// a game object only a script spawns - the bot stood: 64 % of the bot time was bound to
// takers and 70 of 180 bots gained no XP. A turn-in that failed maxFailures times within
// the window is parked: no taker fetch, it no longer counts as finished work, and the
// quest stays in the log to be tried again when the park ends (owner: park, not abandon).
//
// One book per bot, in memory only (the "travel target" value is never evicted). Times
// are WorldTimer::getMSTime() values; every comparison is wrap-safe.
constexpr std::uint32_t BookSize = 16;
// The signed comparison in IsParkActive needs a park below 2^31 ms (the config caps it
// at a day).
constexpr std::uint32_t MaxParkMs = 0x7FFFFFFFu;

struct Entry
{
    std::uint32_t questId = 0;
    std::uint8_t failures = 0;
    std::uint32_t windowStart = 0;
    std::uint32_t parkedUntil = 0;   // 0 = not parked in this cycle
};

struct Book
{
    std::array<Entry, BookSize> entries{};
};

inline bool IsParkActive(Entry const& entry, std::uint32_t nowMs)
{
    return entry.questId && entry.parkedUntil && std::int32_t(entry.parkedUntil - nowMs) > 0;
}

inline bool IsParked(Book const& book, std::uint32_t questId, std::uint32_t nowMs)
{
    if (!questId)
        return false;

    for (Entry const& entry : book.entries)
        if (entry.questId == questId)
            return IsParkActive(entry, nowMs);

    return false;
}

inline std::uint32_t ParkedCount(Book const& book, std::uint32_t nowMs)
{
    std::uint32_t count = 0;
    for (Entry const& entry : book.entries)
        if (IsParkActive(entry, nowMs))
            ++count;
    return count;
}

// The entry for a quest that is not in the book: a free one, else the oldest one that is
// not parked (its window started first), else the park that ends first.
inline Entry& SlotFor(Book& book, std::uint32_t nowMs)
{
    Entry* oldest = nullptr;
    Entry* endsFirst = nullptr;
    for (Entry& entry : book.entries)
    {
        if (!entry.questId)
            return entry;

        if (!IsParkActive(entry, nowMs))
        {
            if (!oldest || nowMs - entry.windowStart > nowMs - oldest->windowStart)
                oldest = &entry;
        }
        else if (!endsFirst || std::int32_t(entry.parkedUntil - endsFirst->parkedUntil) < 0)
            endsFirst = &entry;
    }

    return oldest ? *oldest : *endsFirst;
}

// Counts one failed turn-in of questId (stall, death on the route, work timeout, move
// failures, no route). Returns true when this failure parks the quest: maxFailures
// failures within windowMs park it for parkMs. maxFailures 0 (or parkMs 0) never parks;
// above 255 nothing parks either (the count saturates; the config caps the key at 255).
// A parked quest is not counted again; once its park ended, or its window ran out (also:
// nowMs before windowStart), the count starts anew.
inline bool RecordFailure(Book& book, std::uint32_t questId, std::uint32_t nowMs, std::uint32_t maxFailures,
    std::uint32_t windowMs, std::uint32_t parkMs)
{
    if (!maxFailures || !parkMs || !questId)
        return false;

    Entry* entry = nullptr;
    for (Entry& candidate : book.entries)
        if (candidate.questId == questId)
            entry = &candidate;

    if (entry && IsParkActive(*entry, nowMs))
        return false;

    if (!entry)
    {
        entry = &SlotFor(book, nowMs);
        *entry = Entry();
        entry->questId = questId;
        entry->windowStart = nowMs;
    }
    else if (entry->parkedUntil || nowMs - entry->windowStart > windowMs)
    {
        entry->failures = 0;
        entry->windowStart = nowMs;
        entry->parkedUntil = 0;
    }

    if (entry->failures != 0xFF)
        ++entry->failures;

    if (std::uint32_t(entry->failures) < maxFailures)
        return false;

    entry->failures = 0;
    entry->parkedUntil = nowMs + (parkMs < MaxParkMs ? parkMs : MaxParkMs);
    if (!entry->parkedUntil)
        entry->parkedUntil = 1;
    return true;
}

// How a quest route request that offered only turn-ins ended (ChooseTravelTargetAction).
enum class RouteOutcome : std::uint8_t
{
    Taker,      // a taker was chosen
    Fallback,   // another target: the job found no taker at all and offered quest givers
    NoTarget    // the choice took nothing
};

// (3e) with critic B1.3: "no_route" counts against the offered turn-ins only when the
// request found no way to a taker. Not when the choice ran out of time (the next list
// resumes), not when it did not judge every taker (unjudgedRejects: a list the bot had
// moved away from, or a random skip past an acceptable taker to a farther range), and
// not after the stall/death suppression of a turn-in route, which was counted already.
// The low-level route danger deferral (another continent, a zone clearly above the bot)
// counts only with routeDangerCounts (TurnInParkCountsRouteDanger, an owner decision;
// off = critic B1.3). The fallback to quest givers means the job had no taker at all.
inline bool CountsAsNoRoute(RouteOutcome outcome, bool budgetExceeded, std::uint32_t routeDangerRejects,
    std::uint32_t suppressedRejects, std::uint32_t unjudgedRejects, bool routeDangerCounts)
{
    switch (outcome)
    {
        case RouteOutcome::Taker:
            return false;
        case RouteOutcome::Fallback:
            return true;
        case RouteOutcome::NoTarget:
            return !budgetExceeded && !suppressedRejects && !unjudgedRejects && (routeDangerCounts || !routeDangerRejects);
    }

    return false;
}
}
