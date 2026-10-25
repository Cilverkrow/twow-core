#pragma once

#include <cstdint>

namespace ai::destination_death
{
// #307: after the level-1 reset, 17 % of deaths in six hours came from four
// bots that kept returning to the same fishing spot (one bot: 60 deaths in a
// single 50-yard cell). The turn-in death rule (#123) only covered completed
// quest turn-ins. Any travel destination a bot keeps dying at is now put on a
// per-bot cooldown after maxDeaths deaths; the count restarts afterwards.
// twow-repo#601 F5 (diagnostic, CLI-Deep 25.10.2026): the same per-bot table also holds suppressions that are not deaths
// (TravelTarget::SuppressCurrentDestination); the source names them for the [QuestFirstRoute] state=rejected subsets.
enum Source : std::uint8_t { SourceDeath = 0, SourceWork = 1, SourceEscape = 2, SourcePark = 3, SourceContested = 4 };

struct Record
{
    std::uint8_t deaths = 0;
    std::uint32_t suppressUntil = 0;
    std::uint8_t source = SourceDeath;
};

inline bool IsSuppressed(Record const& record, std::uint32_t now)
{
    return record.suppressUntil > now;
}

// Returns true when this death suppresses the destination.
inline bool RecordDeath(Record& record, std::uint32_t now, std::uint32_t maxDeaths, std::uint32_t cooldownMs)
{
    if (record.suppressUntil && now >= record.suppressUntil)
        record = Record();

    if (maxDeaths == 0)
        return false;

    if (record.deaths < 255)
        ++record.deaths;

    if (record.deaths < maxDeaths)
        return false;

    record.deaths = 0;
    record.suppressUntil = now + cooldownMs;
    record.source = SourceDeath;
    return true;
}
}
