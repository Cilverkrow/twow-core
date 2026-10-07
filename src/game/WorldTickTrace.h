#pragma once

// World-tick phase and map-region timing for the wait analysis (twow-repo#541, owner approval
// 07.10.2026). Behind PerformanceLog.WorldTick (default 0): the world thread records how long each
// phase of a tick took and how long the continent regions took, and writes one [WorldTick] and one
// [MapTick] block per minute. Pure measurement, no behaviour change.
//
// Header-only and free of server types so t/world_tick_trace_test.cpp can check it without a world.
// Both windows are touched by the world thread only (region durations are written by the region
// threads into their own Map and read here after continents.wait()), so there is no lock.

#include "TickStats.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace world_tick
{
enum Phase : uint8_t
{
    Sessions,         // World::UpdateSessions (real clients)
    Teleports,        // MapManager: delayed teleports before and after the map update
    MapsPre,          // MapManager: UpdateSync of every map, building and starting the updaters
    Instances,        // MapManager: instance updates run by the world thread while it waits
    WaitContinents,   // MapManager: the rest of the wait for the slowest continent region (barrier)
    SwitchInstances,  // MapManager: SwitchPlayersInstances + CreateNewInstancesForPlayersSync
    MapsPost,         // MapManager: crashed and unloadable maps
    Managers,         // transports, battlegrounds, LFG/LFT, guards, zone scripts, dynamic visibility, Eluna, groups
    AsyncTasks,       // wait for the WorldAsync task pool
    Results,          // UpdateResultQueue (async database callbacks)
    Rest,             // the remainder of World::Update
    Sleep,            // WorldRunnable sleep up to WORLD_SLEEP_CONST
    PhaseCount
};

inline char const* PhaseName(Phase phase)
{
    static char const* const names[PhaseCount] = { "sessions", "teleports", "maps_pre", "instances",
        "wait_continents", "switch_instances", "maps_post", "managers", "async_tasks", "results", "rest", "sleep" };
    return phase < PhaseCount ? names[phase] : "?";
}

struct PhaseStat
{
    uint32_t avgUs = 0;
    uint32_t p95Us = 0;
    uint32_t maxUs = 0;
};

inline PhaseStat Summarize(std::vector<uint32_t>& samples)
{
    PhaseStat s;
    if (samples.empty())
        return s;
    uint64_t sum = 0;
    for (uint32_t v : samples)
    {
        sum += v;
        if (v > s.maxUs)
            s.maxUs = v;
    }
    s.avgUs = uint32_t(sum / samples.size());
    s.p95Us = TickStatsPercentile(samples, 95);
    return s;
}

// One sample per phase and tick (microseconds).
struct PhaseWindow
{
    std::array<uint64_t, PhaseCount> tick{};
    std::array<std::vector<uint32_t>, PhaseCount> samples;

    void Add(Phase phase, uint64_t us)
    {
        if (phase < PhaseCount)
            tick[phase] += us;
    }

    // Rest = the work time of World::Update minus the phases measured inside it (never negative).
    void EndTick(uint64_t updateUs, uint64_t sleepUs)
    {
        uint64_t measured = 0;
        for (int p = 0; p < Rest; ++p)
            measured += tick[p];
        tick[Rest] = updateUs > measured ? updateUs - measured : 0;
        tick[Sleep] = sleepUs;
        for (int p = 0; p < PhaseCount; ++p)
            samples[p].push_back(uint32_t(tick[p] > UINT32_MAX ? UINT32_MAX : tick[p]));
        tick.fill(0);
    }

    uint32_t Ticks() const { return uint32_t(samples[0].size()); }

    void Clear()
    {
        tick.fill(0);
        for (auto& s : samples)
            s.clear();
    }
};

// Continent regions: duration of each region per tick, and the barrier spread per tick
// (slowest region minus the average = what the others wait at continents.wait()).
struct RegionWindow
{
    std::map<uint64_t, std::vector<uint32_t>> byRegion;   // (mapId << 32 | instanceId) -> us per tick
    std::map<uint64_t, uint32_t> slowestCount;            // how often each region was the slowest
    std::vector<uint32_t> waste;                          // max - avg per tick
    std::vector<uint32_t> slowest;                        // max per tick

    static uint64_t Key(uint32_t mapId, uint32_t instanceId) { return (uint64_t(mapId) << 32) | instanceId; }

    void AddTick(std::vector<std::pair<uint64_t, uint32_t>> const& regions)
    {
        if (regions.empty())
            return;
        uint64_t sum = 0;
        uint32_t max = 0;
        uint64_t maxKey = regions.front().first;
        for (auto const& [key, us] : regions)
        {
            byRegion[key].push_back(us);
            sum += us;
            if (us >= max)
            {
                max = us;
                maxKey = key;
            }
        }
        uint32_t const avg = uint32_t(sum / regions.size());
        waste.push_back(max - avg);
        slowest.push_back(max);
        ++slowestCount[maxKey];
    }

    uint32_t Ticks() const { return uint32_t(waste.size()); }

    void Clear()
    {
        byRegion.clear();
        slowestCount.clear();
        waste.clear();
        slowest.clear();
    }
};
}
