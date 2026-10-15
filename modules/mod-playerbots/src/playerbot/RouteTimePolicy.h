#pragma once

#include "BotUpdateTrace.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::route_time
{
// twow-repo#592 (PATH-02 phase 2, group A no. 2, AiPlayerbot.Perf.RouteTimeTrace, default 0): where the time of a node
// route build goes. Until now only the five slowest bot updates a minute carried an "M:route <us>" token, so builds
// below 16.4 ms and the phases (direct navmesh attempt, A*, end and start legs) were estimates from samples. With the
// switch every TravelNodeMap::getFullPath build adds its phase times to the relaxed atomics below and the world thread
// writes one [RouteTime] line a minute; the M:route token and [RouteFailDetail] get the build's own times appended.
// dispatch_* times the MovePoint(MOVE_PATHFINDING) call in MovementAction::DispatchMovement: a core path search that
// MovePath replaces right after (CLI audit A20). Off: no clock read, no counter, no token, no line.

// Edges of the per-minute build counters: the [BotUpdate] bucket edges 16 384 / 32 768 us, then 50 and 100 ms.
constexpr std::uint64_t Ge16Us = 16384;
constexpr std::uint64_t Ge33Us = 32768;
constexpr std::uint64_t Ge50Us = 50000;
constexpr std::uint64_t Ge100Us = 100000;
// A single navmesh step or A* run at or above this cannot be cut into map-thread slices (go rule for L2 V2).
constexpr std::uint64_t SliceLimitUs = 12000;

// Steps of the direct navmesh attempt (at most 40), log2 classes 0, 1, 2, 3-4, 5-8, 9-16, 17-40 named by their upper
// edge. 0 = no step (other map, or the attempt was not run).
constexpr std::size_t StepClasses = 7;
inline constexpr std::uint32_t StepClassEdges[StepClasses] = { 0, 1, 2, 4, 8, 16, 40 };

inline std::size_t StepClass(std::uint32_t steps)
{
    for (std::size_t c = 0; c + 1 < StepClasses; ++c)
        if (steps <= StepClassEdges[c])
            return c;
    return StepClasses - 1;
}

enum class Phase : std::uint8_t { Direct, Lock, Nodes, AStar, EndLeg, StartLeg, Hearth, Build };

// The times of one getFullPath build, filled on the map thread that runs it.
struct RouteTimes
{
    bool on = false;                // filled (switch on); off = all zero and nothing is appended
    bool directOk = false;          // the direct attempt reached spell distance, the build ended there
    bool found = false;             // the build returned a path (direct attempt or node route)
    std::uint32_t steps = 0;        // navmesh steps (WorldPosition::getPathStepFrom) of the whole build
    std::uint32_t directSteps = 0;  // ... of the direct attempt
    std::uint32_t astarN = 0;
    std::uint32_t endlegN = 0;
    std::uint32_t startlegN = 0;
    std::uint32_t pf = 0;           // PathInfo poly-path builds (core PathFinderStats::builds, thread_local)
    std::uint64_t directUs = 0;
    std::uint64_t lockUs = 0;       // wait for the shared node map lock
    std::uint64_t nodesUs = 0;      // getNodes for start and end (iterate + sort all nodes of the map)
    std::uint64_t astarUs = 0;
    std::uint64_t endlegUs = 0;     // target leg from an end node, with the water retry
    std::uint64_t startlegUs = 0;   // leg to a start node, with the water retry
    std::uint64_t hearthUs = 0;     // hearthstone fallback (AI value and its A* runs)
    std::uint64_t buildUs = 0;      // TravelNodeRoute::buildPath of a found route
    std::uint64_t astarMaxUs = 0;   // longest single A* run
    std::uint64_t stepMaxUs = 0;    // longest single navmesh step
    std::uint64_t totalUs = 0;

    void Add(Phase phase, std::uint64_t us)
    {
        switch (phase)
        {
            case Phase::Direct: directUs += us; break;
            case Phase::Lock: lockUs += us; break;
            case Phase::Nodes: nodesUs += us; break;
            case Phase::AStar:
                astarUs += us;
                ++astarN;
                if (us > astarMaxUs)
                    astarMaxUs = us;
                break;
            case Phase::EndLeg: endlegUs += us; ++endlegN; break;
            case Phase::StartLeg: startlegUs += us; ++startlegN; break;
            case Phase::Hearth: hearthUs += us; break;
            case Phase::Build: buildUs += us; break;
        }
    }

    void AddStep(std::uint64_t us)
    {
        ++steps;
        if (us > stepMaxUs)
            stepMaxUs = us;
    }
};

// The build running on this thread: set by getFullPath for its own scope with the switch on, null otherwise. Read
// by getRoute(position) and WorldPosition::getPathFromPath, like the core's thread_local PathFinderStats.
inline RouteTimes*& Active()
{
    static thread_local RouteTimes* active = nullptr;
    return active;
}

// Adds the time until the end of its scope to one phase (every return and break included). Null times: no clock.
class PhaseTimer
{
public:
    PhaseTimer(RouteTimes* t, Phase p) : times(t), phase(p), startUs(t ? bot_update::NowUs() : 0) {}
    ~PhaseTimer()
    {
        if (times)
            times->Add(phase, bot_update::SinceUs(startUs));
    }
    PhaseTimer(PhaseTimer const&) = delete;
    PhaseTimer& operator=(PhaseTimer const&) = delete;

private:
    RouteTimes* const times;
    Phase const phase;
    std::uint64_t const startUs;
};

// Appended to the "M:route <us> pairs=<n> pts=<n>" engine trace token (after pts=, so the existing
// "M:route [0-9]+us pairs=[0-9]+ pts=[0-9]+" parsers keep matching): d=direct us/steps n=nodes a=A* us/runs
// e=end legs us/runs s=start legs us/runs h=hearthstone b=build l=lock x=longest step pf=poly-path builds.
inline std::string FormatToken(RouteTimes const& t)
{
    if (!t.on)
        return std::string();
    char buf[192];
    std::snprintf(buf, sizeof(buf), " d=%llu/%u n=%llu a=%llu/%u e=%llu/%u s=%llu/%u h=%llu b=%llu l=%llu x=%llu pf=%u",
        (unsigned long long)t.directUs, t.directSteps, (unsigned long long)t.nodesUs, (unsigned long long)t.astarUs, t.astarN,
        (unsigned long long)t.endlegUs, t.endlegN, (unsigned long long)t.startlegUs, t.startlegN, (unsigned long long)t.hearthUs,
        (unsigned long long)t.buildUs, (unsigned long long)t.lockUs, (unsigned long long)t.stepMaxUs, t.pf);
    return buf;
}

// Appended to the [RouteFailDetail] line.
inline std::string FormatDetail(RouteTimes const& t)
{
    if (!t.on)
        return std::string();
    char buf[320];
    std::snprintf(buf, sizeof(buf), " total_us=%llu direct_us=%llu direct_steps=%u nodes_us=%llu astar_us=%llu astar_n=%u astar_max_us=%llu"
        " endleg_us=%llu endleg_n=%u startleg_us=%llu startleg_n=%u hearth_us=%llu lock_us=%llu step_max_us=%llu pf=%u",
        (unsigned long long)t.totalUs, (unsigned long long)t.directUs, t.directSteps, (unsigned long long)t.nodesUs,
        (unsigned long long)t.astarUs, t.astarN, (unsigned long long)t.astarMaxUs, (unsigned long long)t.endlegUs, t.endlegN,
        (unsigned long long)t.startlegUs, t.startlegN, (unsigned long long)t.hearthUs, (unsigned long long)t.lockUs,
        (unsigned long long)t.stepMaxUs, t.pf);
    return buf;
}

// One minute of builds, as read (and reset) by the world thread.
struct Snapshot
{
    std::uint64_t builds = 0, ok = 0, fail = 0;
    std::uint64_t totalUs = 0, maxUs = 0;
    std::uint64_t ge16 = 0, ge33 = 0, ge50 = 0, ge100 = 0;
    std::uint64_t directUs = 0, nodesUs = 0, astarUs = 0, endlegUs = 0, startlegUs = 0, hearthUs = 0, buildUs = 0, lockUs = 0;
    std::uint64_t stepMaxUs = 0, stepMaxGe12 = 0, astarMaxGe12 = 0, pf = 0;
    std::uint64_t dstepsOk[StepClasses] = {};
    std::uint64_t dstepsFail[StepClasses] = {};
    std::uint64_t dispatchN = 0, dispatchUs = 0, dispatchMaxUs = 0;
};

inline void RaiseMax(std::atomic<std::uint64_t>& max, std::uint64_t value)
{
    std::uint64_t seen = max.load(std::memory_order_relaxed);
    while (value > seen && !max.compare_exchange_weak(seen, value, std::memory_order_relaxed))
    {
    }
}

// Map threads add (a fixed number of relaxed atomics per build, no lock), the world thread takes once a minute.
class Counters
{
public:
    void AddBuild(RouteTimes const& t)
    {
        builds.fetch_add(1, std::memory_order_relaxed);
        if (t.found)
            ok.fetch_add(1, std::memory_order_relaxed);
        else
            fail.fetch_add(1, std::memory_order_relaxed);
        totalUs.fetch_add(t.totalUs, std::memory_order_relaxed);
        RaiseMax(maxUs, t.totalUs);
        if (t.totalUs >= Ge16Us)
            ge16.fetch_add(1, std::memory_order_relaxed);
        if (t.totalUs >= Ge33Us)
            ge33.fetch_add(1, std::memory_order_relaxed);
        if (t.totalUs >= Ge50Us)
            ge50.fetch_add(1, std::memory_order_relaxed);
        if (t.totalUs >= Ge100Us)
            ge100.fetch_add(1, std::memory_order_relaxed);
        directUs.fetch_add(t.directUs, std::memory_order_relaxed);
        nodesUs.fetch_add(t.nodesUs, std::memory_order_relaxed);
        astarUs.fetch_add(t.astarUs, std::memory_order_relaxed);
        endlegUs.fetch_add(t.endlegUs, std::memory_order_relaxed);
        startlegUs.fetch_add(t.startlegUs, std::memory_order_relaxed);
        hearthUs.fetch_add(t.hearthUs, std::memory_order_relaxed);
        buildUs.fetch_add(t.buildUs, std::memory_order_relaxed);
        lockUs.fetch_add(t.lockUs, std::memory_order_relaxed);
        RaiseMax(stepMaxUs, t.stepMaxUs);
        if (t.stepMaxUs >= SliceLimitUs)
            stepMaxGe12.fetch_add(1, std::memory_order_relaxed);
        if (t.astarMaxUs >= SliceLimitUs)
            astarMaxGe12.fetch_add(1, std::memory_order_relaxed);
        pf.fetch_add(t.pf, std::memory_order_relaxed);
        if (t.directOk)
            dstepsOk[StepClass(t.directSteps)].fetch_add(1, std::memory_order_relaxed);
        else
            dstepsFail[StepClass(t.directSteps)].fetch_add(1, std::memory_order_relaxed);
    }

    void AddDispatch(std::uint64_t us)
    {
        dispatchN.fetch_add(1, std::memory_order_relaxed);
        dispatchUs.fetch_add(us, std::memory_order_relaxed);
        RaiseMax(dispatchMaxUs, us);
    }

    Snapshot Take()
    {
        Snapshot s;
        s.builds = builds.exchange(0, std::memory_order_relaxed);
        s.ok = ok.exchange(0, std::memory_order_relaxed);
        s.fail = fail.exchange(0, std::memory_order_relaxed);
        s.totalUs = totalUs.exchange(0, std::memory_order_relaxed);
        s.maxUs = maxUs.exchange(0, std::memory_order_relaxed);
        s.ge16 = ge16.exchange(0, std::memory_order_relaxed);
        s.ge33 = ge33.exchange(0, std::memory_order_relaxed);
        s.ge50 = ge50.exchange(0, std::memory_order_relaxed);
        s.ge100 = ge100.exchange(0, std::memory_order_relaxed);
        s.directUs = directUs.exchange(0, std::memory_order_relaxed);
        s.nodesUs = nodesUs.exchange(0, std::memory_order_relaxed);
        s.astarUs = astarUs.exchange(0, std::memory_order_relaxed);
        s.endlegUs = endlegUs.exchange(0, std::memory_order_relaxed);
        s.startlegUs = startlegUs.exchange(0, std::memory_order_relaxed);
        s.hearthUs = hearthUs.exchange(0, std::memory_order_relaxed);
        s.buildUs = buildUs.exchange(0, std::memory_order_relaxed);
        s.lockUs = lockUs.exchange(0, std::memory_order_relaxed);
        s.stepMaxUs = stepMaxUs.exchange(0, std::memory_order_relaxed);
        s.stepMaxGe12 = stepMaxGe12.exchange(0, std::memory_order_relaxed);
        s.astarMaxGe12 = astarMaxGe12.exchange(0, std::memory_order_relaxed);
        s.pf = pf.exchange(0, std::memory_order_relaxed);
        for (std::size_t c = 0; c < StepClasses; ++c)
        {
            s.dstepsOk[c] = dstepsOk[c].exchange(0, std::memory_order_relaxed);
            s.dstepsFail[c] = dstepsFail[c].exchange(0, std::memory_order_relaxed);
        }
        s.dispatchN = dispatchN.exchange(0, std::memory_order_relaxed);
        s.dispatchUs = dispatchUs.exchange(0, std::memory_order_relaxed);
        s.dispatchMaxUs = dispatchMaxUs.exchange(0, std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<std::uint64_t> builds{ 0 }, ok{ 0 }, fail{ 0 };
    std::atomic<std::uint64_t> totalUs{ 0 }, maxUs{ 0 };
    std::atomic<std::uint64_t> ge16{ 0 }, ge33{ 0 }, ge50{ 0 }, ge100{ 0 };
    std::atomic<std::uint64_t> directUs{ 0 }, nodesUs{ 0 }, astarUs{ 0 }, endlegUs{ 0 }, startlegUs{ 0 }, hearthUs{ 0 }, buildUs{ 0 }, lockUs{ 0 };
    std::atomic<std::uint64_t> stepMaxUs{ 0 }, stepMaxGe12{ 0 }, astarMaxGe12{ 0 }, pf{ 0 };
    std::atomic<std::uint64_t> dstepsOk[StepClasses] = {};
    std::atomic<std::uint64_t> dstepsFail[StepClasses] = {};
    std::atomic<std::uint64_t> dispatchN{ 0 }, dispatchUs{ 0 }, dispatchMaxUs{ 0 };
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}

// "0:a,1:b,2:c,4:d,8:e,16:f,40:g"
inline std::string FormatSteps(std::uint64_t const (&classes)[StepClasses])
{
    std::string out;
    for (std::size_t c = 0; c < StepClasses; ++c)
    {
        if (c)
            out += ',';
        out += std::to_string(StepClassEdges[c]) + ':' + std::to_string(classes[c]);
    }
    return out;
}

// The [RouteTime] minute line. ms fields are sums over the minute (whole milliseconds, truncated), max_ms the slowest
// build, ge* counts of builds at or above the edge, step_max_us the longest navmesh step of the minute, *_ge12 builds
// whose longest step / A* run reached SliceLimitUs.
inline std::string FormatLine(Snapshot const& s)
{
    char head[512];
    std::snprintf(head, sizeof(head), "[RouteTime] builds=%llu ms=%llu ge16=%llu ge33=%llu ge50=%llu ge100=%llu max_ms=%llu"
        " direct_ms=%llu astar_ms=%llu endleg_ms=%llu startleg_ms=%llu step_max_us=%llu",
        (unsigned long long)s.builds, (unsigned long long)(s.totalUs / 1000), (unsigned long long)s.ge16, (unsigned long long)s.ge33,
        (unsigned long long)s.ge50, (unsigned long long)s.ge100, (unsigned long long)(s.maxUs / 1000),
        (unsigned long long)(s.directUs / 1000), (unsigned long long)(s.astarUs / 1000), (unsigned long long)(s.endlegUs / 1000),
        (unsigned long long)(s.startlegUs / 1000), (unsigned long long)s.stepMaxUs);
    char tail[512];
    std::snprintf(tail, sizeof(tail), " dispatch_n=%llu dispatch_ms=%llu dispatch_max_us=%llu ok=%llu fail=%llu nodes_ms=%llu"
        " hearth_ms=%llu build_ms=%llu lock_ms=%llu step_max_ge12=%llu astar_max_ge12=%llu pf=%llu",
        (unsigned long long)s.dispatchN, (unsigned long long)(s.dispatchUs / 1000), (unsigned long long)s.dispatchMaxUs,
        (unsigned long long)s.ok, (unsigned long long)s.fail, (unsigned long long)(s.nodesUs / 1000),
        (unsigned long long)(s.hearthUs / 1000), (unsigned long long)(s.buildUs / 1000), (unsigned long long)(s.lockUs / 1000),
        (unsigned long long)s.stepMaxGe12, (unsigned long long)s.astarMaxGe12, (unsigned long long)s.pf);
    return std::string(head) + " dsteps_ok=" + FormatSteps(s.dstepsOk) + " dsteps_fail=" + FormatSteps(s.dstepsFail) + tail;
}
}
