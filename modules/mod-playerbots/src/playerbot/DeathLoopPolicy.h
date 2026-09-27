#pragma once

#include <cstdint>
#include <deque>

namespace ai::death_loop
{
// G4 (train 6.1, 2026-09-27): Konso (hunter, L9) revived at the Eastvale
// graveyard among Prowlers (L9-10) and died there 25 times in 40 minutes. The
// old escalation (another graveyard after 3 deaths, evacuation after 15) runs on
// the "death count" value, which every XP gain resets - a single Prowler kill
// between two deaths started it again. This record is not touched by XP: it
// counts deaths close to each other in time and place.
struct Death
{
    std::uint32_t atSeconds = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct Settings
{
    std::uint32_t maxDeaths = 0;       // deaths that make a loop; 0 = off
    std::uint32_t windowSeconds = 900;
    float radius = 150.0f;             // yards around the latest death
};

// Keeps at most maxDeaths entries (the newest), drops those outside the window.
inline void Record(std::deque<Death>& deaths, Death const& death, Settings const& settings)
{
    if (!settings.maxDeaths)
    {
        deaths.clear();
        return;
    }

    deaths.push_back(death);
    while (deaths.size() > settings.maxDeaths)
        deaths.pop_front();
    while (!deaths.empty() && death.atSeconds - deaths.front().atSeconds > settings.windowSeconds)
        deaths.pop_front();
}

// True when maxDeaths deaths within the window lie within radius of the latest
// one on the same map.
inline bool IsLoop(std::deque<Death> const& deaths, std::uint32_t nowSeconds, Settings const& settings)
{
    if (!settings.maxDeaths || deaths.size() < settings.maxDeaths)
        return false;

    Death const& latest = deaths.back();
    if (nowSeconds - latest.atSeconds > settings.windowSeconds)
        return false;

    std::uint32_t close = 0;
    for (Death const& death : deaths)
    {
        if (latest.atSeconds - death.atSeconds > settings.windowSeconds || death.mapId != latest.mapId)
            continue;
        float const dx = death.x - latest.x;
        float const dy = death.y - latest.y;
        if (dx * dx + dy * dy <= settings.radius * settings.radius)
            ++close;
    }
    return close >= settings.maxDeaths;
}
}
