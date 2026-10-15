#pragma once

#include <atomic>
#include <cstdint>

namespace ai::path_unitless_bot
{
// twow-repo#592 (PATH-02, PR pair 2): the bot side of pathfinding without a Unit. The core's PathInfo
// computes in map mode only with PathFinder.Unitless = 1 and fails closed otherwise ([PathUnitless]).
// Here: what the bots must not do with it at runtime, and when the in-server node generator and the
// route part of GetFishLocations may run at all.

// AiPlayerbot.Perf.PathGenUnitless gate. nullptr = the generator may run; else the cause written in
// "[PathGen] skipped reason=<generator|fish> ... cause=<cause>". Checked in this order; the tile check
// (navmesh tiles of maps 0/1 against the .mmtile files) is the caller's, only after these pass.
inline char const* GenerateSkipCause(bool pathGenUnitless, bool coreUnitless, bool preloadContinents)
{
    if (!pathGenUnitless)
        return "switch_off";
    if (!coreUnitless)
        return "core_unitless_off";
    if (!preloadContinents)
        return "preload_off";
    return nullptr;
}
constexpr char const* TilesIncompleteCause = "tiles_incomplete";

// Tiles are complete when there is at least one .mmtile file and every one of them is in the navmesh.
inline bool TilesComplete(std::uint32_t loadedTiles, std::uint32_t tileFiles)
{
    return tileFiles > 0 && loadedTiles >= tileFiles;
}

// Counters, relaxed atomics: map threads (and the fish jobs) add, the world thread takes them in
// ReportPathUnitlessBot. No switch: every counted case either crashed before #592 or does not happen
// with today's data, so nothing is written while they stay 0.
// T2: a map-mode PathFinder met a bot on a transport in getPathStepFrom ({} without calculate).
inline std::atomic<std::uint64_t> transportRejects{ 0 };
// TravelNodeRoute::buildPath at runtime: a step without a complete node path, moved to the node
// instead of rebuilding the path (that would write the node graph under the shared m_nMapMtx).
inline std::atomic<std::uint64_t> routeIncomplete{ 0 };
// getPathFromPath: map-mode PathFinders it built. Unit mode (every bot path, all map threads) is not
// counted: one shared atomic there would be a contended cache line in the hot path, for a number that
// is only printed next to the rare ones.
inline std::atomic<std::uint64_t> modeMap{ 0 };
// GetFishLocations: candidates dropped because the route part is gated off.
inline std::atomic<std::uint64_t> fishSkipped{ 0 };
}
