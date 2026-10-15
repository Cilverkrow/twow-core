#pragma once

// twow-repo#592 (PATH-02, PathFinder.Unitless): bookkeeping of the PathInfo calls that fail closed
// without a Unit. Before #592 each of them dereferenced the missing Unit (the bots' map-wide paths
// crashed the server); now they return NOPATH and are counted here.
//
// Header-only and free of server types so t/path_unitless_592_test.cpp can check the counting, the
// one-time flag and the minute line without a world.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace path_unitless
{
    enum Reason : uint8_t
    {
        REJECT_DISABLED = 0,    // PathInfo(nullptr), or PathInfo(mapId, instanceId) with PathFinder.Unitless = 0
        REJECT_CALC3,           // calculate(x, y, z): starts at the owner's position
        REJECT_CORE_ONLY,       // UpdateForCaster / UpdateForMelee / CutPathWithDynamicLoS
        REJECT_NO_TERRAIN,      // map mode, the map's terrain is not loaded
        REJECT_TRANSPORT,       // map mode with SetTransport (passenger coordinates need a Unit)
        REJECT_REASON_COUNT
    };

    // The names in the [PathUnitless] lines.
    inline char const* ReasonName(Reason reason)
    {
        switch (reason)
        {
            case REJECT_DISABLED:   return "disabled";
            case REJECT_CALC3:      return "calc3";
            case REJECT_CORE_ONLY:  return "core_only";
            case REJECT_NO_TERRAIN: return "no_terrain";
            case REJECT_TRANSPORT:  return "transport";
            default:                return "unknown";
        }
    }

    struct Totals
    {
        uint64_t count[REJECT_REASON_COUNT] = {};

        uint64_t Sum() const
        {
            uint64_t sum = 0;
            for (uint64_t n : count)
                sum += n;
            return sum;
        }
    };

    // Map threads (and the bots' generator jobs) add, the world thread takes once per minute. Relaxed
    // atomics: plain counts, nothing is ordered by them. Fixed size, no lock, nothing grows.
    struct Counters
    {
        std::atomic<uint64_t> count[REJECT_REASON_COUNT] = {};
        std::atomic<bool> seen[REJECT_REASON_COUNT] = {};

        // True only for the first reject of this reason since the start: the caller writes the
        // one-time "[PathUnitless] reject=<reason>" line then.
        bool Add(Reason reason)
        {
            if (reason >= REJECT_REASON_COUNT)
                return false;
            count[reason].fetch_add(1, std::memory_order_relaxed);
            return !seen[reason].exchange(true, std::memory_order_relaxed);
        }

        // The counts since the last Take().
        Totals Take()
        {
            Totals totals;
            for (int i = 0; i < REJECT_REASON_COUNT; ++i)
                totals.count[i] = count[i].exchange(0, std::memory_order_relaxed);
            return totals;
        }
    };

    // "[PathUnitless] rejects=<n> disabled=.. calc3=.. core_only=.. no_terrain=.. transport=..";
    // empty when nothing failed closed in the interval (no line at all then).
    inline std::string FormatMinuteLine(Totals const& totals)
    {
        uint64_t const sum = totals.Sum();
        if (!sum)
            return std::string();

        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
            "[PathUnitless] rejects=%llu disabled=%llu calc3=%llu core_only=%llu no_terrain=%llu transport=%llu",
            (unsigned long long)sum,
            (unsigned long long)totals.count[REJECT_DISABLED],
            (unsigned long long)totals.count[REJECT_CALC3],
            (unsigned long long)totals.count[REJECT_CORE_ONLY],
            (unsigned long long)totals.count[REJECT_NO_TERRAIN],
            (unsigned long long)totals.count[REJECT_TRANSPORT]);
        return std::string(buffer);
    }
}
