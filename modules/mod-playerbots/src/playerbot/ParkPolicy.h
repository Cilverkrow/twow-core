#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai::park
{
// twow-repo#541/#551 (owner 07.10.2026, "das lohnt sich und ist genau das was wir für die 800
// inaktiven bots möchten - dafür muss das ruhestein setzen aber auch zuverlässig funktionieren";
// "was ist mit den gasthäusern ihrer zugehörigkeit"): a parked bot stands at an inn of its own
// faction for its level band near where it quests, its hearthstone bound there and verified in the
// database, with no travel, quest, grind or rpg and a strongly reduced AI tick.
// Console: rndbot park <name|count> [game_tele name], rndbot unpark <name|count|all>.

// AI update interval of a parked bot out of combat (an attacked bot updates normally).
constexpr std::uint32_t AiIntervalMs = 10000;
// Arrival: within this distance of the inn the bind runs.
constexpr float ArriveYards = 40.0f;
// Bind check: the homebind in the database must match the inn this closely.
constexpr float BindToleranceYards = 1.0f;
// The database check runs this long after the bind (the UPDATE is asynchronous), up to MaxBindTries.
constexpr std::uint32_t BindCheckDelaySeconds = 5;
constexpr std::uint32_t MaxBindTries = 3;
// A bot that has not arrived after this long is teleported to the inn again (once per window).
constexpr std::uint32_t ArriveTimeoutSeconds = 60;

enum class Stage : std::uint8_t
{
    Travel,     // teleport issued, waiting for arrival
    Bind,       // arrived: set the homebind
    Verify,     // homebind set: waiting for the database check
    Parked,     // bound, standing or sitting
    BindFailed  // MaxBindTries database checks failed: parked anyway, logged
};

struct Point
{
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline float Distance2d(Point const& a, Point const& b)
{
    float const dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// The inn for a bot: the nearest candidate on the bot's map, else the first candidate (another map).
// Returns the index or -1 for no candidate.
inline int NearestInn(Point const& bot, std::vector<Point> const& inns)
{
    int best = -1;
    float bestDistance = 0.0f;
    for (std::size_t i = 0; i < inns.size(); ++i)
    {
        if (inns[i].map != bot.map)
            continue;
        float const d = Distance2d(bot, inns[i]);
        if (best < 0 || d < bestDistance)
        {
            best = int(i);
            bestDistance = d;
        }
    }
    if (best < 0 && !inns.empty())
        best = 0;
    return best;
}

inline bool Arrived(Point const& bot, Point const& inn)
{
    return bot.map == inn.map && Distance2d(bot, inn) <= ArriveYards;
}

// Homebind as stored (map, x, y, z) against the inn the bot was bound to.
inline bool BindMatches(Point const& stored, Point const& inn)
{
    return stored.map == inn.map && std::fabs(stored.x - inn.x) <= BindToleranceYards &&
        std::fabs(stored.y - inn.y) <= BindToleranceYards && std::fabs(stored.z - inn.z) <= BindToleranceYards;
}

// After a failed check: bind again (true) or give up (false).
inline bool RetryBind(std::uint32_t triesDone)
{
    return triesDone < MaxBindTries;
}

// Owner 07.10.2026: "ab 25 bots ist ein gasthaus voll, dann in die hauptstadt" - and the limit test
// PARK:1600 (60-130 bots per spot: 285 ticks/min, p95 1132 ms) showed why: visibility grows with
// the square of the bots at one spot. At most SpotCapacity parked bots per inn or city spot.
constexpr std::uint32_t SpotCapacity = 25;

// The spot for a bot: the nearest candidate on its map with room (occupied < capacity), else the
// first candidate on another map with room; -1 when every candidate is full or there is none.
// occupied[i] belongs to candidates[i].
inline int ChooseSpot(Point const& bot, std::vector<Point> const& candidates, std::vector<std::uint32_t> const& occupied,
    std::uint32_t capacity = SpotCapacity)
{
    int best = -1;
    float bestDistance = 0.0f;
    int otherMap = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (i < occupied.size() && occupied[i] >= capacity)
            continue;
        if (candidates[i].map != bot.map)
        {
            if (otherMap < 0)
                otherMap = int(i);
            continue;
        }
        float const d = Distance2d(bot, candidates[i]);
        if (best < 0 || d < bestDistance)
        {
            best = int(i);
            bestDistance = d;
        }
    }
    return best >= 0 ? best : otherMap;
}

// Owner 07.10.2026: standing bots keep at least 2 yards from each other. The slot-th bot at a spot
// stands on rings around it: 8 places per ring (first ring 3 yd), 2.5 yd between rings, every second ring turned by
// half a step - neighbours stay >= 2 yd apart for every slot below SpotCapacity.
constexpr float SlotFirstRingYards = 3.0f;  // 8 places at 3 yd: 2.3 yd apart
constexpr float SlotRingYards = 2.5f;

inline Point SlotOffset(Point const& spot, std::uint32_t slot)
{
    std::uint32_t const ring = slot / 8;
    float const radius = SlotFirstRingYards + SlotRingYards * float(ring);
    float const step = 3.14159265f / 4.0f;
    float const angle = float(slot % 8) * step + ((ring % 2) ? step / 2.0f : 0.0f);
    return Point{ spot.map, spot.x + radius * std::cos(angle), spot.y + radius * std::sin(angle), spot.z };
}

// Reduced AI tick: update when in combat, or when AiIntervalMs passed since the last update.
inline bool AiUpdateDue(bool inCombat, std::uint32_t nowMs, std::uint32_t lastMs)
{
    return inCombat || lastMs == 0 || std::uint32_t(nowMs - lastMs) >= AiIntervalMs;
}
}
