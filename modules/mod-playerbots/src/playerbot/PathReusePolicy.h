#pragma once

#include <atomic>
#include <cstdint>

namespace ai::path_reuse
{
// twow-repo#541 (L1, AiPlayerbot.Perf.PartialPathReuse, default 0): the slow-update tail at 1600 bots is
// MovementAction::ResolveMovePath rebuilding a long path (node route + navmesh, up to 5 x 5 node pairs) on every
// MoveTo of a bot whose last path ends short of the destination (a partial path never passes the "end within 10 %
// of the distance" reuse test). The rebuilt path is then mostly thrown away again by the keep-old rule in
// ResolveMovePath (old path ends at least as close to the destination as the new one). With the switch on, the
// path last built for the same destination is used again without a rebuild while it is younger than
// PartialPathReuseMs and the bot has not reached its end. A truly better path is found at most that much later.
constexpr std::uint32_t DefaultMs = 10000;
constexpr std::uint32_t MinMs = 1000;
constexpr std::uint32_t MaxMs = 60000;

// A path still counts as the one built for the stamp while its end point moved less than this (paths are only
// cut at the front; the end point stays the same point).
constexpr float SameEndYards = 0.5f;

inline std::uint32_t ClampMs(std::int32_t ms)
{
    if (ms < std::int32_t(MinMs))
        return MinMs;
    if (ms > std::int32_t(MaxMs))
        return MaxMs;
    return std::uint32_t(ms);
}

// Reuse needs all of: a stamp from a build, lastPath still that path (same end point), the destination unchanged
// (within the caller's maxDistChange), the bot still on the map it was on at the build, not on a transport, the
// end of the path not yet reached, and a build younger than the age limit.
inline bool MayReuse(bool stamped, bool samePathEnd, bool sameDestination, bool sameStartMap, bool endReached,
    bool onTransport, std::uint32_t ageMs, std::uint32_t maxAgeMs)
{
    return stamped && samePathEnd && sameDestination && sameStartMap && !endReached && !onTransport && ageMs < maxAgeMs;
}

// [PathReuse] counters. Map threads add, the world thread takes them once per minute. hits = rebuilds skipped,
// misses = rebuilds that ran with the switch on (the existing end-near-destination reuse had missed as well).
inline std::atomic<std::uint64_t> hits{ 0 };
inline std::atomic<std::uint64_t> misses{ 0 };
}
