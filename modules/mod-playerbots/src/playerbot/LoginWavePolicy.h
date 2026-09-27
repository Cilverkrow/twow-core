#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace ai::login_wave
{
// #391 (owner 2026-09-27): after an L1 reset all roster bots used to log in at
// once and crowded the start zones (train 6: 154 bots, quest mobs always dead,
// four bots still L1 after ten hours; a level-up wave that drove the tick load).
// Bots flagged by the reset (at_login & 6 == 6) now log in in waves, mixed over
// the start zones, so the first ones quest and spread out before the next wave.
struct Candidate
{
    std::uint32_t guid = 0;
    std::uint32_t startZone = 0;    // playercreateinfo map/area of race and class
};

// Groups the candidates by start zone (ascending zone key), keeps the roster
// order inside a zone, then takes one bot per zone in turn. Returns guid ->
// wave index (0-based). Deterministic for the same roster order.
inline std::map<std::uint32_t, std::uint32_t> AssignWaves(std::vector<Candidate> const& inRosterOrder, std::uint32_t waveSize)
{
    std::map<std::uint32_t, std::uint32_t> waves;
    if (!waveSize)
        return waves;

    std::map<std::uint32_t, std::vector<std::uint32_t>> byZone;
    for (Candidate const& candidate : inRosterOrder)
        byZone[candidate.startZone].push_back(candidate.guid);

    std::uint32_t index = 0;
    for (std::size_t round = 0;; ++round)
    {
        bool any = false;
        for (auto const& [zone, guids] : byZone)
        {
            if (round >= guids.size())
                continue;
            any = true;
            waves[guids[round]] = index++ / waveSize;
        }
        if (!any)
            break;
    }
    return waves;
}

inline bool IsOpen(std::uint32_t wave, std::uint32_t elapsedSeconds, std::uint32_t intervalSeconds)
{
    return std::uint64_t(elapsedSeconds) >= std::uint64_t(wave) * intervalSeconds;
}

// Number of waves that are open after elapsedSeconds (never more than total).
inline std::uint32_t OpenWaves(std::uint32_t totalWaves, std::uint32_t elapsedSeconds, std::uint32_t intervalSeconds)
{
    if (!totalWaves)
        return 0;
    if (!intervalSeconds)
        return totalWaves;
    std::uint32_t const open = elapsedSeconds / intervalSeconds + 1;
    return open < totalWaves ? open : totalWaves;
}
}
