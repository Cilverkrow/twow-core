#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::route_precheck
{
// twow-repo#592 (PATH-02 phase 2, group A no. 5, AiPlayerbot.Perf.RoutePrecheck, default 0): a failed node route search
// costs a direct navmesh attempt, up to 6 A* runs and up to 5 target legs of up to 40 Detour searches each (60-150 ms);
// 83 of 93 slow failures failed on the target side. A target leg only counts when its path ends < 1 yd (2D) and < 2 yd
// (Z) from the target (getRoute, WorldPosition::isPathTo). A navmesh path ends on a polygon: findSmoothPath steers to
// closestPointOnPolyBoundary(last polygon, target) and puts the end 0.5 above getPolyHeight. So a success needs a
// polygon whose bounds meet [target +- (1, 2.5, 1)]; dtNavMeshQuery::findNearestPoly with the half extents below
// (+0.25 margin) and a filter that passes every flag finds one whenever one exists (the BV tree bounds are
// conservative). No polygon there: no target leg can succeed and the pair loop returns no route - the same empty
// route, only earlier. Every case in which PathInfo ends a path off the navmesh is exempt and runs as today: tile
// missing (shortcut), water (swim / underwater paths, the surface retry), transport, flying, dead, ignore-pathfinding,
// Tel'Abim (shortcut on NOPATH), other map, generator, the deadzone.csv debug log, a node within reach of the point.
// Levels: 1 = shadow (check, count, compare with the real result - "wrong" = called impossible but a route was found;
// nothing is skipped), 2 = skip the pair loop when the target check says impossible, 3 = also when the start check
// says impossible (no polygon around the start for any start leg; not while the hearthstone fallback could use the
// target leg), 4 = also skip the direct attempt when no polygon lies within spell distance of the target (deferred:
// run late when the pair loop has to run after all). Lower checks keep running in shadow above their level.

constexpr std::uint32_t MaxLevel = 4;

inline std::uint32_t ClampLevel(std::int32_t level)
{
    if (level <= 0)
        return 0;
    if (level > std::int32_t(MaxLevel))
        return MaxLevel;
    return std::uint32_t(level);
}

// Half extents of the search boxes in yards (horizontal / vertical; Detour orders them y, z, x).
constexpr float BoxMargin = 0.25f;
constexpr float EndLegReach = 1.0f;           // target leg: isPathTo(path, 1.0f) in getRoute
constexpr float PathZReach = 2.0f;            // isPathTo's default Z tolerance
constexpr float PolyEndLift = 0.5f;           // findSmoothPath: end = getPolyHeight + 0.5
constexpr float EndHalfXY = EndLegReach + BoxMargin;                 // 1.25
constexpr float EndHalfZ = PathZReach + PolyEndLift + BoxMargin;     // 2.75
constexpr float StartHalfXY = 5.0f + BoxMargin;                      // PathInfo::FindWalkPoly extents 5 / 10 / 5
constexpr float StartHalfZ = 10.0f + BoxMargin;
constexpr float DirectHalfZ = EndHalfZ;       // horizontal: spell distance + margin (the direct attempt's goal)
// A start node within this (2D) of the start is reached by cropping the direct path (1 yd, 20 yd on a transport node).
constexpr float StartNodeReach = 1.0f;
constexpr float TransportStartNodeReach = 20.0f;

// The core's NoMovementAreas (PathFinder.cpp): a NOPATH there becomes a shortcut. All of the zone is exempt.
constexpr std::uint32_t TelAbimMapId = 1;
constexpr std::uint32_t TelAbimZoneId = 5121;

enum class Result : std::uint8_t
{
    NotRun,        // level 0, or not asked (a higher check already decided)
    ExemptTile,    // a tile under the box or under a node is not loaded (PathInfo shortcut), or no navmesh
    ExemptWater,   // the point or a node can be swum to (swim / underwater paths, surface retry)
    ExemptOther,   // transport, flying, dead, ignore-pathfinding, Tel'Abim, other map, generator, debug log, node near
    Poly,          // a polygon lies in the box: the search may succeed, it runs
    NoPoly         // no polygon in the box and nothing exempt: the search cannot succeed
};

inline char const* ResultName(Result r)
{
    switch (r)
    {
        case Result::ExemptTile: return "tile";
        case Result::ExemptWater: return "water";
        case Result::ExemptOther: return "other";
        case Result::Poly: return "poly";
        case Result::NoPoly: return "nopoly";
        default: return "-";
    }
}

enum class Kind : std::uint8_t { None, End, Start, Direct };

inline char const* KindName(Kind k)
{
    switch (k)
    {
        case Kind::End: return "end";
        case Kind::Start: return "start";
        case Kind::Direct: return "direct";
        default: return "none";
    }
}

// The questions in the order of their cost; a later one is only asked while the earlier ones allow a skip. Every
// unknown answers "no skip" (fail closed).
template <class TilesLoaded, class PolyNear, class Water>
inline Result Decide(bool exempt, TilesLoaded tilesLoaded, PolyNear polyNear, Water water)
{
    if (exempt)
        return Result::ExemptOther;
    if (!tilesLoaded())
        return Result::ExemptTile;
    if (polyNear())
        return Result::Poly;
    if (water())
        return Result::ExemptWater;
    return Result::NoPoly;
}

// Level 1 never skips; 2 the pair loop on the target check, 3 also on the start check, 4 also the direct attempt.
inline bool Skips(std::uint32_t level, Kind kind, Result r)
{
    if (r != Result::NoPoly)
        return false;
    switch (kind)
    {
        case Kind::End: return level >= 2;
        case Kind::Start: return level >= 3;
        case Kind::Direct: return level >= 4;
        default: return false;
    }
}

// Classes of the direct attempt's result, as in the [RouteFailDetail] analyses: eq = it did not get past the start
// (direct_gap == dist), lt5 / lt25 / ge25 = how far from the target it ended, na = other map or not run.
enum class GapClass : std::uint8_t { Eq, Lt5, Lt25, Ge25, Na };
constexpr std::size_t GapClasses = 5;

inline GapClass GapClassOf(bool directRun, std::size_t directPoints, float directGap)
{
    if (!directRun || directGap < 0.0f)
        return GapClass::Na;
    if (directPoints <= 1)
        return GapClass::Eq;
    if (directGap < 5.0f)
        return GapClass::Lt5;
    if (directGap < 25.0f)
        return GapClass::Lt25;
    return GapClass::Ge25;
}

// One getFullPath build with the switch on. getFullPath makes it the thread's active build for its scope; getRoute
// uses it only for its own call (same map, start and target), other callers of getRoute run as before.
struct BuildContext
{
    std::uint32_t level = 0;
    std::uint32_t mapId = 0;        // start
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    std::uint32_t endMapId = 0;     // target
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    GapClass gap = GapClass::Na;
    Result end = Result::NotRun;
    Result start = Result::NotRun;
    Result direct = Result::NotRun;
    Kind skipped = Kind::None;     // the check that skipped work (none in shadow)
    bool directDeferred = false;   // level 4 left out the direct attempt
    bool directLate = false;       // ... and getRoute ran it late, because its pair loop had to run
    bool directLateOk = false;     // ... and it reached spell distance after all (never by the proof): direct answer
    float lateGap = -1.0f;

    bool Matches(std::uint32_t map, float x, float y, float z, std::uint32_t endMap, float tx, float ty, float tz) const
    {
        return map == mapId && x == sx && y == sy && z == sz && endMap == endMapId && tx == ex && ty == ey && tz == ez;
    }
};

inline BuildContext*& ActiveBuild()
{
    static thread_local BuildContext* active = nullptr;
    return active;
}

struct Snapshot
{
    std::uint64_t endChecks[GapClasses] = {};
    std::uint64_t endHits[GapClasses] = {};
    std::uint64_t startChecks = 0, startHits = 0, directChecks = 0, directHits = 0;
    std::uint64_t exemptTile = 0, exemptWater = 0, exemptOther = 0;
    std::uint64_t wrongEnd = 0, wrongStart = 0, wrongDirect = 0;
    std::uint64_t skipsEnd = 0, skipsStart = 0, skipsDirect = 0;
};

// Map threads add (relaxed atomics, no lock), the world thread takes once a minute.
class Counters
{
public:
    void Count(Kind kind, Result r, GapClass gap)
    {
        switch (r)
        {
            case Result::ExemptTile: exemptTile.fetch_add(1, std::memory_order_relaxed); break;
            case Result::ExemptWater: exemptWater.fetch_add(1, std::memory_order_relaxed); break;
            case Result::ExemptOther: exemptOther.fetch_add(1, std::memory_order_relaxed); break;
            default: break;
        }
        bool const hit = r == Result::NoPoly;
        switch (kind)
        {
            case Kind::End:
                endChecks[std::size_t(gap)].fetch_add(1, std::memory_order_relaxed);
                if (hit)
                    endHits[std::size_t(gap)].fetch_add(1, std::memory_order_relaxed);
                break;
            case Kind::Start:
                startChecks.fetch_add(1, std::memory_order_relaxed);
                if (hit)
                    startHits.fetch_add(1, std::memory_order_relaxed);
                break;
            case Kind::Direct:
                directChecks.fetch_add(1, std::memory_order_relaxed);
                if (hit)
                    directHits.fetch_add(1, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }

    // A route (or a direct path) came back although the check had called it impossible.
    void Wrong(Kind kind)
    {
        switch (kind)
        {
            case Kind::End: wrongEnd.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Start: wrongStart.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Direct: wrongDirect.fetch_add(1, std::memory_order_relaxed); break;
            default: break;
        }
    }

    void Skipped(Kind kind)
    {
        switch (kind)
        {
            case Kind::End: skipsEnd.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Start: skipsStart.fetch_add(1, std::memory_order_relaxed); break;
            case Kind::Direct: skipsDirect.fetch_add(1, std::memory_order_relaxed); break;
            default: break;
        }
    }

    Snapshot Take()
    {
        Snapshot s;
        for (std::size_t c = 0; c < GapClasses; ++c)
        {
            s.endChecks[c] = endChecks[c].exchange(0, std::memory_order_relaxed);
            s.endHits[c] = endHits[c].exchange(0, std::memory_order_relaxed);
        }
        s.startChecks = startChecks.exchange(0, std::memory_order_relaxed);
        s.startHits = startHits.exchange(0, std::memory_order_relaxed);
        s.directChecks = directChecks.exchange(0, std::memory_order_relaxed);
        s.directHits = directHits.exchange(0, std::memory_order_relaxed);
        s.exemptTile = exemptTile.exchange(0, std::memory_order_relaxed);
        s.exemptWater = exemptWater.exchange(0, std::memory_order_relaxed);
        s.exemptOther = exemptOther.exchange(0, std::memory_order_relaxed);
        s.wrongEnd = wrongEnd.exchange(0, std::memory_order_relaxed);
        s.wrongStart = wrongStart.exchange(0, std::memory_order_relaxed);
        s.wrongDirect = wrongDirect.exchange(0, std::memory_order_relaxed);
        s.skipsEnd = skipsEnd.exchange(0, std::memory_order_relaxed);
        s.skipsStart = skipsStart.exchange(0, std::memory_order_relaxed);
        s.skipsDirect = skipsDirect.exchange(0, std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<std::uint64_t> endChecks[GapClasses] = {};
    std::atomic<std::uint64_t> endHits[GapClasses] = {};
    std::atomic<std::uint64_t> startChecks{ 0 }, startHits{ 0 }, directChecks{ 0 }, directHits{ 0 };
    std::atomic<std::uint64_t> exemptTile{ 0 }, exemptWater{ 0 }, exemptOther{ 0 };
    std::atomic<std::uint64_t> wrongEnd{ 0 }, wrongStart{ 0 }, wrongDirect{ 0 };
    std::atomic<std::uint64_t> skipsEnd{ 0 }, skipsStart{ 0 }, skipsDirect{ 0 };
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}

// The [RoutePrecheck] minute line. checks / hits = target checks and those that found no polygon, end_nopoly = the
// same per direct-attempt class as hits/checks, wrong = all three kinds (wrong_end / wrong_start / wrong_direct),
// unknown_tile / exempt_water / exempt_other over all checks, skips_* = work actually left out (0 in shadow). The head
// is the spec's "[RoutePrecheck] checks= hits= wrong= end_nopoly="; level= comes last.
inline std::string FormatLine(std::uint32_t level, Snapshot const& s)
{
    static char const* const names[GapClasses] = { "eq", "lt5", "lt25", "ge25", "na" };
    std::uint64_t checks = 0, hits = 0;
    std::string classes;
    for (std::size_t c = 0; c < GapClasses; ++c)
    {
        checks += s.endChecks[c];
        hits += s.endHits[c];
        if (c)
            classes += ',';
        classes += std::string(names[c]) + ':' + std::to_string(s.endHits[c]) + '/' + std::to_string(s.endChecks[c]);
    }
    char head[128];
    std::snprintf(head, sizeof(head), "[RoutePrecheck] checks=%llu hits=%llu wrong=%llu end_nopoly=",
        (unsigned long long)checks, (unsigned long long)hits, (unsigned long long)(s.wrongEnd + s.wrongStart + s.wrongDirect));
    char tail[512];
    std::snprintf(tail, sizeof(tail), " start_checks=%llu start_nopoly=%llu direct_checks=%llu direct_nopoly=%llu unknown_tile=%llu"
        " exempt_water=%llu exempt_other=%llu wrong_end=%llu wrong_start=%llu wrong_direct=%llu skips_end=%llu skips_start=%llu"
        " skips_direct=%llu level=%u",
        (unsigned long long)s.startChecks, (unsigned long long)s.startHits, (unsigned long long)s.directChecks,
        (unsigned long long)s.directHits, (unsigned long long)s.exemptTile, (unsigned long long)s.exemptWater,
        (unsigned long long)s.exemptOther, (unsigned long long)s.wrongEnd, (unsigned long long)s.wrongStart,
        (unsigned long long)s.wrongDirect, (unsigned long long)s.skipsEnd, (unsigned long long)s.skipsStart,
        (unsigned long long)s.skipsDirect, level);
    return std::string(head) + classes + tail;
}

// Appended to [RouteFailDetail] with the switch on: " precheck=<end> precheck_start=<start> precheck_direct=<direct>
// precheck_skip=<none|end|start|direct>"; empty when the build ran without the switch.
inline std::string FormatDetail(BuildContext const& b)
{
    if (!b.level)
        return std::string();
    return std::string(" precheck=") + ResultName(b.end) + " precheck_start=" + ResultName(b.start) + " precheck_direct=" +
        ResultName(b.direct) + " precheck_skip=" + KindName(b.skipped);
}
}
