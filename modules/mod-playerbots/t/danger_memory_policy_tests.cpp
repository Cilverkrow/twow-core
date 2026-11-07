#include "DangerMemoryPolicy.h"
#include "DangerMapPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// twow-repo#575 A3 step 4 S4-4 (AiPlayerbot.Danger.Memory): the pure rules of DangerMemoryPolicy.h (own deaths, the level, the
// per-pass cell cache, counters) and DangerMap::QueryAt (design F4: the point's own cell counts; judged by the real killer).
namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

ai::corpse_first::DeathMark Mark(std::uint32_t at, std::uint32_t map, float x, float y)
{
    ai::corpse_first::DeathMark m;
    m.atSeconds = at;
    m.mapId = map;
    m.x = x;
    m.y = y;
    return m;
}

ai::danger_map::Death Death(std::uint32_t at, std::uint32_t victim, std::uint8_t killer, std::uint8_t realKiller)
{
    ai::danger_map::Death d;
    d.time = at;
    d.victim = victim;
    d.killerLevel = killer;
    d.victimLevel = 10;
    d.realKillerLevel = realKiller;
    return d;
}
}

int main()
{
    using namespace ai::danger_memory;
    namespace dmap = ai::danger_map;

    // 1. Config loader: an unknown mode is off.
    {
        Require(ClampMode(0) == 0 && ClampMode(1) == 1 && ClampMode(2) == 2 && ClampMode(3) == 3, "modes 0-3 pass");
        Require(ClampMode(4) == 0 && ClampMode(-1) == 0, "an unknown mode acts like 0");
    }

    // 2. The level: own >= 2 or a shared cell = avoid, one own death = caution.
    {
        Require(Combine(0, 0) == Level::Safe, "nothing: safe");
        Require(Combine(1, 0) == Level::Caution, "one own death: caution");
        Require(Combine(2, 0) == Level::Avoid, "two own deaths: avoid (A3 4.5 (1))");
        Require(Combine(0, 1) == Level::Avoid, "a shared cell: avoid");
        Require(Combine(1, 3) == Level::Avoid, "both: avoid");
    }

    // 3. Own deaths: 30 min, 60 yd, same map; deaths after now do not count.
    {
        ai::corpse_first::Ring ring;
        ai::corpse_first::Record(ring, Mark(1000, 0, 0.0f, 0.0f));
        ai::corpse_first::Record(ring, Mark(1500, 0, 36.0f, 48.0f));   // 60 yd away
        ai::corpse_first::Record(ring, Mark(1600, 1, 0.0f, 0.0f));     // other map
        ai::corpse_first::Record(ring, Mark(1700, 0, 61.0f, 0.0f));    // just outside
        Require(OwnDeathsNear(ring, 1700, 0, 0.0f, 0.0f) == 2, "two within 60 yd on map 0");
        Require(OwnDeathsNear(ring, 1000 + OwnWindowSeconds, 0, 0.0f, 0.0f) == 2, "exactly 30 min still counts");
        Require(OwnDeathsNear(ring, 1000 + OwnWindowSeconds + 1, 0, 0.0f, 0.0f) == 1, "older than 30 min drops");
        Require(OwnDeathsNear(ring, 900, 0, 0.0f, 0.0f) == 0, "deaths after now do not count");
        Require(OwnDeathsNear(ring, 1700, 1, 0.0f, 0.0f) == 1, "map filter");
    }

    // 4. F4: Query(point, point) skips the point's own cell and its neighbours; QueryAt sees them.
    {
        dmap::Params params;   // cell 100, window 7200, min deaths 3, margin 3
        dmap::DangerMap map;
        for (std::uint32_t v = 1; v <= 3; ++v)
            map.Record(0, 150.0f, 150.0f, Death(1000, v, 20, 20), params);
        Require(map.Query(0, 150.0f, 150.0f, 150.0f, 150.0f, 10, 1100, params).cells == 0, "legacy arrival query never sees the point (F4)");
        Require(map.QueryAt(0, 150.0f, 150.0f, 10, 1100, params).cells == 1, "QueryAt sees the point's cell");
        Require(map.QueryAt(0, 250.0f, 250.0f, 10, 1100, params).cells == 1, "and a neighbour cell");
        Require(map.QueryAt(0, 350.0f, 350.0f, 10, 1100, params).cells == 0, "two cells away: nothing");
        Require(map.QueryAt(0, 150.0f, 150.0f, 17, 1100, params).cells == 1, "real killer 20 vs bot 17: margin 3 counts");
        Require(map.QueryAt(0, 150.0f, 150.0f, 18, 1100, params).cells == 0, "bot 18: below the margin");
        Require(map.QueryAt(0, 150.0f, 150.0f, 10, 1000 + params.windowSeconds, params).cells == 0, "window expires");
    }

    // 5. The real killer, not the current target: a low target with a high add counts only for QueryAt, and vice versa.
    {
        dmap::Params params;
        dmap::DangerMap map;
        for (std::uint32_t v = 1; v <= 3; ++v)
            map.Record(0, 150.0f, 150.0f, Death(1000, v, 8, 25), params);   // target level 8, real killer 25
        Require(map.QueryAt(0, 150.0f, 150.0f, 10, 1100, params).cells == 1, "real killer 25 counts for a level 10 bot");
        Require(map.Query(0, 1000.0f, 1000.0f, 150.0f, 150.0f, 10, 1100, params).cells == 0, "legacy reader keeps the target level 8");

        dmap::DangerMap legacyOnly;
        for (std::uint32_t v = 1; v <= 3; ++v)
            legacyOnly.Record(0, 150.0f, 150.0f, Death(1000, v, 25, 0), params);   // key was 0: no real killer recorded
        Require(legacyOnly.QueryAt(0, 150.0f, 150.0f, 10, 1100, params).cells == 0, "no real killer recorded: QueryAt ignores it");
        Require(legacyOnly.Query(0, 1000.0f, 1000.0f, 150.0f, 150.0f, 10, 1100, params).cells == 1, "the legacy reader is unchanged");

        dmap::DangerMap oneBot;
        for (std::uint32_t i = 0; i < 3; ++i)
            oneBot.Record(0, 150.0f, 150.0f, Death(1000 + i, 7, 25, 25), params);   // one bot dying three times
        Require(oneBot.QueryAt(0, 150.0f, 150.0f, 10, 1100, params).cells == 0, "MinDeaths counts different bots (own deaths are the per-bot layer)");
    }

    // 6. The per-pass cell cache: find what was put, nothing when full.
    {
        SharedCellCache cache;
        std::uint32_t cells = 99;
        Require(!cache.Find(0, 1, 1, cells), "empty cache");
        cache.Put(0, 1, 1, 2);
        Require(cache.Find(0, 1, 1, cells) && cells == 2, "cached answer");
        Require(!cache.Find(1, 1, 1, cells), "other map");
        for (std::int32_t i = 0; i < 40; ++i)
            cache.Put(0, 100 + i, 0, 0);
        Require(cache.count == SharedCellCache::Size, "bounded");
        Require(!cache.Find(0, 139, 0, cells), "full: not kept");
    }

    // 7. Names and counters.
    {
        Require(std::strcmp(LevelName(Level::Avoid), "avoid") == 0 && std::strcmp(LevelName(Level::Caution), "caution") == 0, "level names");
        Require(std::strcmp(QueryName(Query::Arrive), "arrive") == 0 && std::strcmp(QueryName(Query::Count), "unknown") == 0, "query names");

        Counters counters;
        counters.Note(Query::Grind, Level::Avoid, 2, 0);
        counters.Note(Query::Arrive, Level::Avoid, 0, 1);
        counters.Note(Query::Target, Level::Caution, 1, 0);
        counters.Note(Query::Target, Level::Safe, 0, 0);
        Require(counters.avoid[std::size_t(Query::Grind)].load() == 1 && counters.avoid[std::size_t(Query::Arrive)].load() == 1 &&
            counters.avoid[std::size_t(Query::Target)].load() == 0, "avoid per query");
        Require(counters.ownAvoid.load() == 1 && counters.sharedAvoid.load() == 1 && counters.caution.load() == 1, "reasons");

        for (std::uint32_t i = 0; i < LinesPerMinute; ++i)
            Require(counters.LineAllowed(), "line budget");
        Require(!counters.LineAllowed(), "budget used up");
        Require(counters.LogDue(1'800'000'020), "minute line due");
        Require(counters.LineAllowed(), "the minute line resets the budget");
        Require(!counters.LogDue(1'800'000'040), "one minute line per minute");
    }

    std::cout << "danger_memory_policy_tests: ok\n";
    return 0;
}
