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

// twow-repo#629 G1 (CLI-GRP g1n 26.10.2026, owner go, switch AiPlayerbot.DeathLoop.HostileAreaOutsideDungeons,
// default 0): a dungeon run in the hostile faction's territory (Wailing Caverns for Alliance bots, map 43 in the
// Barrens) marked every death there a hostile-area death loop - 19 evacuations with deaths=1 out of the instance in one
// g1n run. The guards A39 is about stand outside; with the switch a death inside a dungeon or raid is no hostile-area
// death. The other loop rules (window, short life) still apply.
inline bool HostileAreaDeath(bool enemyHomeZone, bool inDungeon, bool outsideDungeonsOnly)
{
    return enemyHomeZone && !(outsideDungeonsOnly && inDungeon);
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

// twow-repo#601/#602 G2 (OB-00 go 26.10.2026, switch AiPlayerbot.DeathLoop.FarEscape, default 0). Hunter A/B restart
// (track A, 26.10. 02:42-04:03Z): five goblin hunters L3-4 died 48 times at the Blackstone Island spirit healer among
// L4-7 mobs - revive, attack within a second, dead 1-40 s later, then ~10 min as a ghost before the next revive.
// (1) The ghost time spreads the deaths so far apart that MaxDeaths 4 within 900 s fired twice. A death soon after
//     a revive close to where the bot stood up is counted apart from the window: ShortLifeDeaths of them in a row,
//     each revive within the radius of the first, are a loop.
// (2) Both evacuations went to the goblins' homebind, the island start 380 yd from the graveyard (zone 5536; the
//     server binds a character there at its first login, so the factory's Durotar bind never applied); the bots
//     died again 51 s and 3 min later. An escape point on the loop's map closer than FarEscapeMinYards to the
//     latest death, or in its zone, is replaced by the race's safe start, and the homebind is set there.
constexpr std::uint32_t ShortLifeSeconds = 60;
constexpr std::uint32_t ShortLifeDeaths = 3;
constexpr float FarEscapeMinYards = 600.0f;

struct ShortLife
{
    std::uint32_t reviveAt = 0;      // the latest revive (epoch s), 0 = none since the last death
    std::uint32_t reviveMap = 0;
    float reviveX = 0.0f;
    float reviveY = 0.0f;
    std::uint32_t streak = 0;        // deaths in a row soon after a revive at one place
    std::uint32_t anchorMap = 0;     // where the first revive of the streak happened
    float anchorX = 0.0f;
    float anchorY = 0.0f;
};

inline void RecordRevive(ShortLife& life, std::uint32_t nowSeconds, std::uint32_t mapId, float x, float y)
{
    life.reviveAt = nowSeconds ? nowSeconds : 1;
    life.reviveMap = mapId;
    life.reviveX = x;
    life.reviveY = y;
}

// A death ShortLifeSeconds or less after the latest revive and within radius of the revive point extends the
// streak (restarted when the revive lay outside radius of the streak's first one); any other death ends it.
// True when the streak reaches ShortLifeDeaths.
inline bool RecordShortLifeDeath(ShortLife& life, std::uint32_t nowSeconds, std::uint32_t mapId, float x, float y, float radius)
{
    float const r2 = radius * radius;
    float const dx = x - life.reviveX;
    float const dy = y - life.reviveY;
    bool const shortLife = life.reviveAt && nowSeconds >= life.reviveAt && nowSeconds - life.reviveAt <= ShortLifeSeconds &&
        mapId == life.reviveMap && dx * dx + dy * dy <= r2;
    life.reviveAt = 0;
    if (!shortLife)
    {
        life.streak = 0;
        return false;
    }

    float const ax = life.reviveX - life.anchorX;
    float const ay = life.reviveY - life.anchorY;
    if (life.streak && (life.reviveMap != life.anchorMap || ax * ax + ay * ay > r2))
        life.streak = 0;
    if (!life.streak)
    {
        life.anchorMap = life.reviveMap;
        life.anchorX = life.reviveX;
        life.anchorY = life.reviveY;
    }
    ++life.streak;
    return life.streak >= ShortLifeDeaths;
}

inline bool IsShortLifeLoop(ShortLife const& life)
{
    return life.streak >= ShortLifeDeaths;
}

// The escape point lies at the loop: same map and closer than FarEscapeMinYards to the latest death, or in its zone.
inline bool EscapeAtLoop(std::uint32_t loopMap, float loopX, float loopY, std::uint32_t loopZone,
    std::uint32_t pointMap, float pointX, float pointY, std::uint32_t pointZone)
{
    if (pointMap != loopMap)
        return false;
    if (loopZone && pointZone == loopZone)
        return true;
    float const dx = pointX - loopX;
    float const dy = pointY - loopY;
    return dx * dx + dy * dy < FarEscapeMinYards * FarEscapeMinYards;
}

enum EscapeCount { EscapeDetected, EscapeEscaped, EscapeRebound, EscapeRepeat, EscapeHostileDeath, EscapeCounts };

inline std::atomic<std::uint64_t>& EscapeCounter(EscapeCount count)
{
    static std::atomic<std::uint64_t> counters[EscapeCounts] = {};
    return counters[count];
}
}
