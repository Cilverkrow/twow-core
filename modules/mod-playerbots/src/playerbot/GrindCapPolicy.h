#pragma once

#include <cstdint>

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
}
