#pragma once

// twow-repo#597 (TICK-01): which GridMap objects TerrainInfo::CleanUpGrids frees in one pass.
//
// TerrainInfo::GetGrid loads a GridMap lazily and takes no grid reference; only Load() (NGrid
// load, terrain preload) counts one. Every lazily loaded grid therefore has ref 0, and with
// CleanupTerrain = 1 the 60 s pass per terrain freed all of them on the world thread, after which
// the next height/LOS query reloaded the grid from disk (TICK01 synthesis 3.B: 47 % of the live
// ticks > 100 ms).
//
// Two switches, both 0 = today's behaviour:
//   TerrainCleanup.IdleMinutes      a ref-0 grid is freed only after it was unused for that long
//                                   (last-use stamp written by GetGrid, in seconds of terrain
//                                   time; no clock read, the world thread advances it)
//   TerrainCleanup.MaxGridsPerTick  at most that many grids freed per call; the scan stops there
//                                   and resumes on the next world tick at the same grid
//
// Header-only and free of server types so t/terrain_cleanup_597_test.cpp can check it without a
// world.

#include <cstdint>

namespace terrain_cleanup
{
    // MAX_NUMBER_OF_GRIDS (GridDefines.h); pinned by t/check_terrain_cleanup_597_contract.cmake.
    constexpr uint32_t GRIDS_PER_SIDE = 64;
    constexpr uint32_t GRID_COUNT = GRIDS_PER_SIDE * GRIDS_PER_SIDE;

    // Upper bound of TerrainCleanup.IdleMinutes (one day): the idle test subtracts uint32 seconds,
    // which stays exact far beyond that.
    constexpr uint32_t MAX_IDLE_MINUTES = 1440;

    inline uint32_t ClampIdleMinutes(uint32_t idleMinutes)
    {
        return idleMinutes > MAX_IDLE_MINUTES ? MAX_IDLE_MINUTES : idleMinutes;
    }

    // GetGrid writes the last-use stamp only with the switch on; off, it neither loads the stamp
    // nor stores it.
    inline bool StampEnabled(uint32_t idleMinutes)
    {
        return idleMinutes != 0;
    }

    // With the switch on, at most one store per grid and terrain second: the stamp is written only
    // when it differs from the current terrain second.
    inline bool NeedsStamp(uint32_t storedSec, uint32_t nowSec)
    {
        return storedSec != nowSec;
    }

    // Seconds since the last use; unsigned wrap-around keeps it right across a counter overflow.
    inline uint32_t IdleSeconds(uint32_t nowSec, uint32_t lastUseSec)
    {
        return nowSec - lastUseSec;
    }

    // Today: free every loaded grid with ref 0. idleMinutes > 0 additionally keeps a grid that was
    // used within the last idleMinutes.
    inline bool ShouldFree(bool loaded, int32_t ref, uint32_t idleMinutes, uint32_t nowSec, uint32_t lastUseSec)
    {
        if (!loaded || ref != 0)
            return false;
        if (idleMinutes == 0)
            return true;
        return IdleSeconds(nowSec, lastUseSec) >= ClampIdleMinutes(idleMinutes) * 60u;
    }

    // maxPerTick = 0: unlimited (today).
    inline bool BudgetLeft(uint32_t maxPerTick, uint32_t freed)
    {
        return maxPerTick == 0 || freed < maxPerTick;
    }

    // CleanUpGrids scans when the 60 s timer passed (today) or a capped scan is still open.
    inline bool ScanThisTick(bool timerPassed, bool resumePending)
    {
        return timerPassed || resumePending;
    }

    struct PassResult
    {
        uint32_t nextCursor = 0;  // first grid index not yet examined; 0 when done
        uint32_t freed = 0;
        bool done = true;         // the scan reached the end of the table (timer reset)
    };

    // One pass from cursor (index = y * GRIDS_PER_SIDE + x, today's y-outer / x-inner order).
    // shouldFree(x, y) decides, doFree(x, y) frees. With maxPerTick = 0 this is today's loop: every
    // grid examined once, in the same order, done = true.
    template <class ShouldFreeFn, class FreeFn>
    inline PassResult RunPass(uint32_t cursor, uint32_t maxPerTick, ShouldFreeFn&& shouldFree, FreeFn&& doFree)
    {
        PassResult result;
        uint32_t i = cursor < GRID_COUNT ? cursor : 0;
        for (; i < GRID_COUNT; ++i)
        {
            if (!BudgetLeft(maxPerTick, result.freed))
                break;
            uint32_t const x = i % GRIDS_PER_SIDE;
            uint32_t const y = i / GRIDS_PER_SIDE;
            if (shouldFree(x, y))
            {
                doFree(x, y);
                ++result.freed;
            }
        }
        if (i < GRID_COUNT)
        {
            result.nextCursor = i;
            result.done = false;
        }
        return result;
    }
}
