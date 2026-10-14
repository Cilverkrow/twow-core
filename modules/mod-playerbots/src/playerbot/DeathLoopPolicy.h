#pragma once

#include <atomic>
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

// twow-repo#541 audit A39 (owner go 10.10.2026, switch AiPlayerbot.DeathLoop.Escape, default 0). OB-30 #405 arm:
// 811 revives in Durotar in 30 min, ~130 of 181 Alliance (High Elf) bots killed by Horde guards; "evacuate" fired
// 225 times and each time the bot came back. Cause: RepopAction sends Goblins and High Elves to their homebind,
// and a park run without the faction check (c696b451) had bound them to the Razor Hill inn. With the switch:
// a death in an area of the hostile faction counts as a loop at once, and the evacuation never uses a homebind
// in a hostile area - it goes to the race's safe start and binds there.
// A death within windowSeconds after an escape: the escape did not break the loop.
inline bool RepeatAfterEscape(std::uint32_t nowSeconds, std::uint32_t lastEscapeSeconds, std::uint32_t windowSeconds)
{
    return lastEscapeSeconds && nowSeconds >= lastEscapeSeconds && nowSeconds - lastEscapeSeconds <= windowSeconds;
}

// Safe start of the two races RandomPlayerbotFactory rebinds (their real start zones are player-only on Turtle);
// the same coordinates as RandomPlayerbotFactory::CreateRandomBot (contract-checked).
struct StartPoint
{
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t zone = 0;
};

inline bool RaceStartOverride(std::uint32_t race, StartPoint& out)
{
    if (race == 9)    // RACE_GOBLIN: Durotar
    {
        out = StartPoint{ 1, -618.518f, -4251.67f, 38.718f, 14 };
        return true;
    }
    if (race == 10)   // RACE_HIGH_ELF: Elwynn Forest
    {
        out = StartPoint{ 0, -8949.95f, -132.493f, 83.5312f, 12 };
        return true;
    }
    return false;
}

enum EscapeCount { EscapeDetected, EscapeEscaped, EscapeRebound, EscapeRepeat, EscapeHostileDeath, EscapeCounts };

inline std::atomic<std::uint64_t>& EscapeCounter(EscapeCount count)
{
    static std::atomic<std::uint64_t> counters[EscapeCounts] = {};
    return counters[count];
}
}
