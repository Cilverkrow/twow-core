#pragma once

#include <cstdint>
#include <deque>
#include <map>

namespace ai::death_series
{
// twow-repo#422 (train 8): death series the existing rules miss. Ravenieve (L10)
// died 29 times in 1.3 h on turn-in routes through L10-12 mobs, Lineriela 23
// times to Mudpaw gnolls, Shanie (L15) 11 times without a killer on the way to
// fishing spots (fatigue in deep water). DeathLoop counts deaths at one place,
// DestinationDeaths suppresses one destination - the next fishing spot or
// turn-in route then kills the bot again.

// A) Deaths anywhere: SeriesDeaths within WindowSeconds put a roster bot on its
// own into the cautious mode for CautiousSeconds - quest targets in zones above
// its level are deferred, no grinding above its level.
constexpr uint32_t WindowSeconds = 1800;
constexpr uint32_t SeriesDeaths = 4;
constexpr uint32_t CautiousSeconds = 1800;
constexpr size_t MaxEntries = 8;

struct Series
{
    std::deque<uint32_t> deaths;
    uint32_t cautiousUntil = 0;

    // Returns true when this death starts the cautious mode.
    bool Record(uint32_t now)
    {
        deaths.push_back(now);
        while (deaths.size() > MaxEntries || (!deaths.empty() && now - deaths.front() >= WindowSeconds))
            deaths.pop_front();
        if (deaths.size() < SeriesDeaths || cautiousUntil > now)
            return false;
        cautiousUntil = now + CautiousSeconds;
        return true;
    }

    bool Cautious(uint32_t now) const
    {
        return cautiousUntil > now;
    }
};

// Zone-level margin of the quest route check (#307 route_danger, normally 5).
inline uint32_t RouteMargin(bool cautious, uint32_t normal = 5)
{
    return cautious ? 0 : normal;
}

// Levels above the bot a grind target may have (#307 grind_cap).
inline int GrindMargin(bool cautious, int normal)
{
    return cautious && normal > 0 ? 0 : normal;
}

// B) A death without a killer (no current target, no attackers) while the bot
// travels to or works at a gathering destination counts for the whole purpose
// (fishing, herbs, mining, skinning): EnvDeaths within WindowSeconds suppress
// that purpose for PurposeSuppressSeconds.
constexpr uint32_t EnvDeaths = 2;
constexpr uint32_t PurposeSuppressSeconds = 3600;

inline bool IsEnvironmentalDeath(bool hasCurrentTarget, uint32_t attackers)
{
    return !hasCurrentTarget && attackers == 0;
}

struct PurposeSuppression
{
    std::map<uint32_t, std::deque<uint32_t>> deaths;
    std::map<uint32_t, uint32_t> until;

    // Returns true when this death suppresses the purpose.
    bool RecordEnvironmentalDeath(uint32_t purpose, uint32_t now)
    {
        std::deque<uint32_t>& list = deaths[purpose];
        list.push_back(now);
        while (!list.empty() && now - list.front() >= WindowSeconds)
            list.pop_front();
        if (list.size() < EnvDeaths)
            return false;
        list.clear();
        until[purpose] = now + PurposeSuppressSeconds;
        return true;
    }

    bool Suppressed(uint32_t purpose, uint32_t now) const
    {
        if (!purpose)
            return false;
        auto const it = until.find(purpose);
        return it != until.end() && it->second > now;
    }
};
}
