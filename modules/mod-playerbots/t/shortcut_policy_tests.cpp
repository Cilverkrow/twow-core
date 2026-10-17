// twow-repo#595 P4 S0 + B1: the near decision of the real-player snapshot, NearScope, the default radius,
// staleness, the TestWatcherGuids list, the trace counters and the group-loop decision.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
// The module includes windows.h before this header on Windows (RandomPlayerbotMgr.cpp): its empty near / far macros
// must not break the policy (counter member nearAny, not near).
#include <windows.h>
#endif

#include "ShortcutPolicy.h"

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

using namespace ai::shortcut;

Entry MakeEntry(std::uint32_t map, std::uint32_t zone, float x, float y, float defaultRadius = 400.0f)
{
    Entry entry;
    entry.map = map;
    entry.zone = zone;
    entry.x = x;
    entry.y = y;
    entry.defaultRadius = defaultRadius;
    return entry;
}

struct ZoneProbe
{
    std::uint32_t zone;
    int* calls;
    std::uint32_t operator()() const { ++*calls; return zone; }
};
}

int main()
{
    std::uint64_t const now = 100000;

    // Scope clamp and the measuring scope.
    Require(ClampScope(0) == ScopeOff && ClampScope(1) == ScopeRadius && ClampScope(2) == ScopeZoneOrRadius,
        "scope 0 / 1 / 2 kept");
    Require(ClampScope(3) == ScopeOff && ClampScope(-1) == ScopeOff, "negative: out-of-range scope is off");
    Require(MeasureScope(0) == ScopeZoneOrRadius && MeasureScope(1) == ScopeRadius && MeasureScope(2) == ScopeZoneOrRadius,
        "the trace measures with 2 while NearScope is 0");

    // Radius: 0 = map visibility + react distance (continents 250 + 150).
    Require(DefaultRadius(250.0f, 150.0f) == 400.0f, "default radius = visibility + react");
    Require(DefaultRadius(-1.0f, 150.0f) == 150.0f, "negative visibility counts as 0");
    Require(Radius(0.0f, 400.0f) == 400.0f && Radius(120.0f, 400.0f) == 120.0f, "configured radius wins, 0 = default");

    // Staleness and the build cadence.
    Require(Fresh(now - StaleMs, now) && !Fresh(now - StaleMs - 1, now), "older than 5 s is stale");
    Require(!Fresh(0, now), "never built is stale");
    Require(!Fresh(now + 1, now), "negative: a build time in the future is not trusted");
    Require(BuildDue(0, now) && BuildDue(now - BuildIntervalMs, now) && !BuildDue(now - BuildIntervalMs + 1, now),
        "rebuilt at most once a second");

    Snapshot snapshot;
    snapshot.builtAtMs = now - 100;
    snapshot.entries.push_back(MakeEntry(0, 12, 0.0f, 0.0f));        // Elwynn, Goldshire-ish
    snapshot.entries.push_back(MakeEntry(1, 14, 1000.0f, 1000.0f));  // Durotar

    int calls = 0;
    ZoneProbe elwynn{12, &calls};
    ZoneProbe westfall{40, &calls};
    ZoneProbe unknownZone{0, &calls};

    // Unknown cases first (fail closed).
    Require(Evaluate(nullptr, now, ScopeZoneOrRadius, 0.0f, 0, 10.0f, 10.0f, elwynn) == Near::Unknown, "no snapshot: unknown");
    Snapshot stale = snapshot;
    stale.builtAtMs = now - StaleMs - 1;
    Require(Evaluate(&stale, now, ScopeZoneOrRadius, 0.0f, 0, 10.0f, 10.0f, elwynn) == Near::Unknown, "stale snapshot: unknown");
    Require(Evaluate(&snapshot, now, ScopeOff, 0.0f, 0, 10.0f, 10.0f, elwynn) == Near::Unknown, "scope off: unknown");

    // No entry on the map: not near, and no zone lookup.
    calls = 0;
    Require(Evaluate(&snapshot, now, ScopeZoneOrRadius, 0.0f, 530, 10.0f, 10.0f, elwynn) == Near::NotNear, "other map: not near");
    Require(calls == 0, "no zone lookup without an entry on the map");
    Snapshot empty;
    empty.builtAtMs = now;
    Require(Evaluate(&empty, now, ScopeZoneOrRadius, 0.0f, 0, 10.0f, 10.0f, elwynn) == Near::NotNear, "empty fresh snapshot: not near");

    // Radius only.
    Require(Evaluate(&snapshot, now, ScopeRadius, 0.0f, 0, 399.0f, 0.0f, elwynn) == Near::Near, "within the default 400 yd: near");
    Require(Evaluate(&snapshot, now, ScopeRadius, 0.0f, 0, 401.0f, 0.0f, elwynn) == Near::NotNear, "outside 400 yd, scope 1: not near");
    Require(Evaluate(&snapshot, now, ScopeRadius, 100.0f, 0, 150.0f, 0.0f, elwynn) == Near::NotNear, "configured 100 yd: 150 is not near");
    calls = 0;
    Require(Evaluate(&snapshot, now, ScopeRadius, 0.0f, 0, 2000.0f, 0.0f, elwynn) == Near::NotNear && calls == 0,
        "scope 1 never asks for the zone");
    Require(Evaluate(&snapshot, now, ScopeRadius, 0.0f, 0, 300.0f, 300.0f, elwynn) == Near::NotNear, "2D distance (424 yd) counts, not per axis");

    // Zone or radius.
    calls = 0;
    Require(Evaluate(&snapshot, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, elwynn) == Near::Near, "same zone far away: near");
    Require(calls == 1, "zone asked once");
    Require(Evaluate(&snapshot, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, westfall) == Near::NotNear, "other zone far away: not near");
    calls = 0;
    Require(Evaluate(&snapshot, now, ScopeZoneOrRadius, 0.0f, 0, 50.0f, 0.0f, westfall) == Near::Near && calls == 0,
        "within the radius across a zone border: near, no zone lookup");
    Require(Evaluate(&snapshot, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, unknownZone) == Near::Unknown,
        "zone 0 at the query, outside the radius: unknown");
    Snapshot zoneless = snapshot;
    zoneless.entries[0].zone = 0;
    Require(Evaluate(&zoneless, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, elwynn) == Near::Unknown,
        "zone 0 at the player, outside the radius: unknown");
    Require(Evaluate(&zoneless, now, ScopeRadius, 0.0f, 0, 3000.0f, 0.0f, elwynn) == Near::NotNear,
        "zone 0 does not matter for scope 1");
    Snapshot twoOnMap = zoneless;
    twoOnMap.entries.push_back(MakeEntry(0, 12, 5000.0f, 5000.0f));
    Require(Evaluate(&twoOnMap, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, elwynn) == Near::Near,
        "another entry in the same zone decides near despite a zone 0 entry");

    // No usable radius (visibility and react distance 0): fail closed, never NotNear.
    Snapshot noRadius;
    noRadius.builtAtMs = now;
    noRadius.entries.push_back(MakeEntry(0, 12, 0.0f, 0.0f, 0.0f));
    Require(Evaluate(&noRadius, now, ScopeRadius, 0.0f, 0, 0.0f, 0.0f, elwynn) == Near::Unknown,
        "negative: radius 0, scope 1, player on the spot: unknown, not not-near");
    Require(Evaluate(&noRadius, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, westfall) == Near::Unknown,
        "negative: radius 0, scope 2, other zone: unknown");
    Require(Evaluate(&noRadius, now, ScopeZoneOrRadius, 0.0f, 0, 3000.0f, 0.0f, elwynn) == Near::Near,
        "radius 0, scope 2, same zone: near");
    Require(Evaluate(&noRadius, now, ScopeRadius, 50.0f, 0, 10.0f, 0.0f, elwynn) == Near::Near,
        "a configured radius replaces the 0 default");
    Require(Evaluate(&noRadius, now, ScopeRadius, 0.0f, 1, 0.0f, 0.0f, elwynn) == Near::NotNear,
        "radius 0 but no entry on that map: not near");

    // Watchers are plain entries.
    Snapshot watched;
    watched.builtAtMs = now;
    Entry watcher = MakeEntry(1, 17, -500.0f, -2600.0f);
    watcher.watcher = true;
    watched.entries.push_back(watcher);
    Require(Evaluate(&watched, now, ScopeZoneOrRadius, 0.0f, 1, -450.0f, -2600.0f, elwynn) == Near::Near, "a test watcher counts as a player");

    // Combine both ends.
    Require(Combine(Near::NotNear, Near::NotNear) == Near::NotNear, "both not near");
    Require(Combine(Near::Near, Near::Unknown) == Near::Near && Combine(Near::Unknown, Near::Near) == Near::Near, "near wins");
    Require(Combine(Near::NotNear, Near::Unknown) == Near::Unknown, "unknown beats not near");

    // TestWatcherGuids parsing.
    Require(ParseGuidList("").empty(), "empty list: no watchers (default)");
    std::vector<std::uint32_t> const guids = ParseGuidList(" 12, 345 ,6789,12,,abc,-5,0, 4294967295,4294967296, 7x");
    Require(guids.size() == 4 && guids[0] == 12 && guids[1] == 345 && guids[2] == 6789 && guids[3] == 4294967295u,
        "blanks, duplicates, text, negatives, 0 and overflows are skipped");
    std::string many;
    for (int i = 1; i <= 100; ++i)
        many += std::to_string(i) + ",";
    Require(ParseGuidList(many).size() == MaxWatchers, "watcher list capped");

    // Counters.
    Counters counters;
    Record(counters, Mech::Hop, Near::NotNear, Near::Near);
    Record(counters, Mech::Hop, Near::NotNear, Near::NotNear);
    Record(counters, Mech::Hop, Near::Unknown, Near::NotNear);
    Record(counters, Mech::Repop, Near::Near, Near::Near);
    Record(counters, Mech::Count, Near::Near, Near::Near);
    MechCounters const& hop = counters.mech[std::size_t(Mech::Hop)];
    Require(hop.nearAny == 1 && hop.notNear == 1 && hop.unknown == 1 && hop.nearSrc == 0 && hop.nearDst == 1, "hop counts");
    MechCounters const& repop = counters.mech[std::size_t(Mech::Repop)];
    Require(repop.nearAny == 1 && repop.nearSrc == 1 && repop.nearDst == 1, "repop counts both ends");
    Require(counters.mech[std::size_t(Mech::GhostCorpse)].nearAny == 0 && counters.mech[std::size_t(Mech::FarMove)].nearAny == 0,
        "negative: other mechanisms untouched, Count ignored");
    Require(std::string(MechName(Mech::Hop)) == "hop" && std::string(MechName(Mech::GhostCorpse)) == "ghost_corpse" &&
        std::string(MechName(Mech::Repop)) == "repop" && std::string(MechName(Mech::FarMove)) == "far_move", "mechanism names");

    // B1 group loop.
    Require(GroupLoopMovesMember(false, false, true) && GroupLoopMovesMember(false, true, false),
        "switch 0: every member is moved, as today");
    Require(GroupLoopMovesMember(true, true, false), "switch 1: a bot member with its own AI is moved");
    Require(!GroupLoopMovesMember(true, false, true), "switch 1: a real member without bot AI is skipped");
    Require(!GroupLoopMovesMember(true, true, true), "switch 1: a self-bot with a real session is skipped");

    std::cout << "shortcut_policy_tests passed\n";
    return 0;
}
