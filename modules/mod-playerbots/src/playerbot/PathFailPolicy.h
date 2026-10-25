#pragma once

#include "RoutePrecheckPolicy.h"
#include "RouteTimePolicy.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::path_fail
{
// twow-repo#575 (PATH-01 #1, AiPlayerbot.Path.RouteFailDetail): what a failed node route search saw. Filled by
// TravelNodeMap::getRoute(position) / getFullPath only when the caller passes it (trace on); the caller logs it.
struct RouteFailDetail
{
    std::uint32_t pairs = 0;          // node pairs tried (A* + navmesh checks)
    std::uint32_t badStart = 0;       // start nodes the bot could not reach within 1 yd (20 yd on a transport)
    std::uint32_t badEnd = 0;         // end nodes from which the target was not reachable within 1 yd
    std::uint32_t startNodes = 0;     // candidate start / end nodes (at most 5 each)
    std::uint32_t endNodes = 0;
    std::string startNode;            // nearest start / end node (name), empty without candidates
    std::string endNode;
    float startNodeDist = -1.0f;      // bot -> nearest start node, target -> nearest end node (yd)
    float endNodeDist = -1.0f;
    float directGap = -1.0f;          // how far the direct navmesh attempt ended from the target (-1 = no path)
    bool noNodes = false;             // no candidate nodes on one side
    bool cooldownSkip = false;        // #416 route cooldown hit, no search ran
    bool budgetStop = false;          // #416 1000 ms budget stopped the search
    bool transportRoute = false;      // bot on a transport: the first pair decides
    bool earlySkip = false;           // twow-repo#592: left out before the direct attempt (AiPlayerbot.Perf.RouteFailEarlySkip 2)
    ai::route_time::RouteTimes times; // twow-repo#592 (PATH-02): phase times of the build (AiPlayerbot.Perf.RouteTimeTrace)
    ai::route_precheck::BuildContext precheck; // twow-repo#592 (PATH-02): what the pre-check saw (AiPlayerbot.Perf.RoutePrecheck)
};

// Lines per minute for [RouteFailDetail]; every failure is counted, only the first N a minute are logged.
constexpr std::uint32_t DefaultLinesPerMinute = 30;
constexpr std::uint32_t MaxLinesPerMinute = 600;

inline std::uint32_t ClampLinesPerMinute(std::int32_t lines)
{
    if (lines <= 0)
        return 0;
    if (lines > std::int32_t(MaxLinesPerMinute))
        return MaxLinesPerMinute;
    return std::uint32_t(lines);
}

// telemetry P0 (audit Q7): the window a rolled summary covers, in whole minutes (at least 1). A roll that happens at the next event
// after a quiet gap spans the gap; the summary line says so with window_s=, and the minute driver rolls every minute so a quiet
// minute is a line with zeros instead of no line.
inline std::uint32_t WindowMinutes(std::uint64_t seen, std::uint64_t now)
{
    std::uint64_t const w = seen != 0 && now > seen ? now - seen : 1;   // the very first roll (seen == 0) has no earlier window
    return w > 1440 ? 1440 : std::uint32_t(w);
}

// Shared by all map threads: failures this minute, lines logged this minute, summary once a minute.
struct Counters
{
    std::atomic<std::uint32_t> failed{0};
    std::atomic<std::uint32_t> logged{0};
    std::atomic<std::uint32_t> honest{0};      // PATH-01 #3: MoveTo2 returned false for an empty route
    std::atomic<std::uint64_t> minute{0};
    std::atomic<std::uint32_t> windowMinutes{1};   // telemetry P0: minutes the last rolled window covered (a gap without events spans several)

    // Starts a new minute when due; returns true for exactly one caller, which then logs the summary of the
    // previous minute (outFailed / outLogged / outHonest).
    bool Roll(std::uint64_t nowSeconds, std::uint32_t& outFailed, std::uint32_t& outLogged, std::uint32_t& outHonest)
    {
        std::uint64_t const now = nowSeconds / 60;
        std::uint64_t seen = minute.load(std::memory_order_relaxed);
        if (seen == now || !minute.compare_exchange_strong(seen, now, std::memory_order_relaxed))
            return false;
        outFailed = failed.exchange(0, std::memory_order_relaxed);
        outLogged = logged.exchange(0, std::memory_order_relaxed);
        outHonest = honest.exchange(0, std::memory_order_relaxed);
        windowMinutes.store(WindowMinutes(seen, now), std::memory_order_relaxed);
        return seen != 0;
    }

    // One more failure; true when its detail line is still within this minute's budget.
    bool Take(std::uint32_t linesPerMinute)
    {
        failed.fetch_add(1, std::memory_order_relaxed);
        if (logged.fetch_add(1, std::memory_order_relaxed) < linesPerMinute)
            return true;
        logged.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }

// twow-repo#575 (QFO 41747, AiPlayerbot.Path.PartialNoop): a target on a navmesh island (Tower of Azora, 22.10.2026) gets a
// partial path that ends short of it; once the bot stands at that end every MoveTo built the same path again, moved nowhere
// and reported success, so the travel retry counter never ran and the bot stood for up to 100 min. The move is a no-op when
// the bot is within reach of the path's end while that end is more than gap from the target (same map only).
constexpr float PartialNoopDefaultReach = 2.0f;
constexpr float PartialNoopDefaultGap = 5.0f;

inline bool IsPartialNoop(bool sameMap, float botToPathEnd, float pathEndToTarget, float reach, float gap)
{
    return sameMap && botToPathEnd <= reach && pathEndToTarget > gap;
}

// [MoveTo] state=partial_noop lines: every no-op is counted (failed), the first DefaultLinesPerMinute a minute are logged.
inline Counters& PartialNoopCounters() { static Counters counters; return counters; }

// [PathIsland] (owner 22.10.2026, "dann fallen uns diese auch in Zukunft auf"): one line per bot and travel target that ends
// in a partial no-op, so islands show up as a list in the test logs. Key = the target's spawn guid, else entry + position.
inline std::uint64_t IslandKey(std::uint64_t spawnGuid, std::int32_t entry, float x, float y)
{
    if (spawnGuid)
        return spawnGuid;
    return (std::uint64_t(std::uint32_t(entry)) << 32) ^ (std::uint64_t(std::uint16_t(std::int32_t(x))) << 16) ^
        std::uint64_t(std::uint16_t(std::int32_t(y)));
}

// Per bot: the keys already reported; at most IslandBookMax (the oldest goes first), so a bot never grows it without bound.
constexpr std::size_t IslandBookMax = 64;

struct IslandBook
{
    std::vector<std::uint64_t> keys;

    // True the first time a key is seen (the line is due), false for a key already reported.
    bool FirstTime(std::uint64_t key)
    {
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return false;
        if (keys.size() >= IslandBookMax)
            keys.erase(keys.begin());
        keys.push_back(key);
        return true;
    }
};

// twow-repo#575 (PATH-01 b, AiPlayerbot.Path.TaxiTrace): what bot flights and ship boardings do at runtime. The bot
// travel graph stores TaxiPath ids from another numbering (0 of 270 flight links carry the right id), and 6 Turtle
// ships have no link; this counts flight attempts by result and transport waits, with a line budget per minute.
enum class TaxiResult : std::uint8_t { Ok, NoPath, NoNpc, TooFar, Failed };

inline char const* TaxiResultName(TaxiResult result)
{
    switch (result)
    {
        case TaxiResult::Ok: return "ok";
        case TaxiResult::NoPath: return "noPath";
        case TaxiResult::NoNpc: return "noNpc";
        case TaxiResult::TooFar: return "tooFar";
        default: return "failed";
    }
}

// The core refuses a flight farther than this from the start node (Player::ActivateTaxiPathTo, ERR_TAXITOOFARAWAY).
constexpr float TaxiStartReach = 31.6f;

struct TaxiCounters
{
    std::atomic<std::uint32_t> results[5] = {};
    std::atomic<std::uint32_t> transportWaits{0};
    std::atomic<std::uint32_t> logged{0};
    std::atomic<std::uint64_t> minute{0};
    std::atomic<std::uint32_t> windowMinutes{1};   // telemetry P0, see Counters

    void Count(TaxiResult result) { results[std::size_t(result)].fetch_add(1, std::memory_order_relaxed); }

    // Same rules as Counters::Roll: one caller per new minute gets the previous minute (none for the very first).
    bool Roll(std::uint64_t nowSeconds, std::uint32_t (&outResults)[5], std::uint32_t& outWaits, std::uint32_t& outLogged)
    {
        std::uint64_t const now = nowSeconds / 60;
        std::uint64_t seen = minute.load(std::memory_order_relaxed);
        if (seen == now || !minute.compare_exchange_strong(seen, now, std::memory_order_relaxed))
            return false;
        for (std::size_t i = 0; i < 5; ++i)
            outResults[i] = results[i].exchange(0, std::memory_order_relaxed);
        outWaits = transportWaits.exchange(0, std::memory_order_relaxed);
        outLogged = logged.exchange(0, std::memory_order_relaxed);
        windowMinutes.store(WindowMinutes(seen, now), std::memory_order_relaxed);
        return seen != 0;
    }

    bool Take(std::uint32_t linesPerMinute)
    {
        if (logged.fetch_add(1, std::memory_order_relaxed) < linesPerMinute)
            return true;
        logged.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }
};

inline TaxiCounters& SharedTaxiCounters() { static TaxiCounters counters; return counters; }

// twow-repo#575 (PATH-01 #4, AiPlayerbot.Path.StartZoneExactBounds): Blackstone Island (5536, map 1) and Thalassian
// Highlands + Alah'Thalas (5225 / 2040, map 0) as rectangles taken from the zone cells of the map files. Validated
// against tw_world_t9 spawns: map 1 box = all 1305 spawns of 5536; map 0 boxes = 2342 of 2346 spawns of 5225/2040,
// none of the 261 Eastern / 181 Western Plaguelands spawns. The old boxes left 8 + 10 quest givers outside.
struct Box
{
    float xMin, xMax, yMin, yMax;
};

constexpr Box BlackstoneBoxes[] = {
    { -1066.7f, 1066.7f, -8533.3f, -6400.0f },
};

constexpr Box ThalassianBoxes[] = {
    { 3733.3f, 4950.0f, -3566.7f, -1233.3f },
    { 3200.0f, 3733.3f, -2666.7f, -1233.3f },
    { 3000.0f, 3200.0f, -2600.0f, -2270.0f },
    { 3400.0f, 3733.3f, -3050.0f, -2666.7f },
    { 3200.0f, 3400.0f, -2900.0f, -2666.7f },
};

template <std::size_t N>
inline bool InAny(Box const (&boxes)[N], float x, float y)
{
    for (Box const& box : boxes)
        if (x >= box.xMin && x < box.xMax && y >= box.yMin && y < box.yMax)
            return true;
    return false;
}

// exact = 0: the boxes before PATH-01 (unchanged); exact = 1: the zone boxes above.
inline bool InCustomStartZone(std::uint32_t mapId, float x, float y, bool exact)
{
    if (!exact)
    {
        if (mapId == 1 && x > -500.0f && x < 350.0f && y > -7850.0f && y < -7100.0f)
            return true;
        if (mapId == 0 && x > 3000.0f && x < 3800.0f && y > -2800.0f && y < -2250.0f)
            return true;
        return false;
    }

    if (mapId == 1)
        return InAny(BlackstoneBoxes, x, y);
    if (mapId == 0)
        return InAny(ThalassianBoxes, x, y);
    return false;
}

// twow-repo#575 (PATH-01 E6, AiPlayerbot.Path.RescueSkipCustomStarts): the quest rescue of a level 1-10 bot picked any
// start position of its faction, so an orc could land on Blackstone Island (goblin start, no route out for other races)
// or a human in the Thalassian Highlands, and the rescue rebinds the hearthstone there. On: the goblin (9) and high-elf
// (10) starts are rescue targets only for bots of that race.
inline bool IsCustomStartRace(std::uint32_t race)
{
    return race == 9 || race == 10;
}

inline bool RescueSkipsStartOf(std::uint32_t targetRace, std::uint32_t botRace, bool on)
{
    return on && targetRace != botRace && IsCustomStartRace(targetRace);
}
}
