// twow-repo#597 (TICK-01): TerrainInfo::CleanUpGrids decisions (TerrainCleanupPolicy.h).
// Off (IdleMinutes 0, MaxGridsPerTick 0) the pass frees exactly what the old loop freed, in the same
// order, in one call; IdleMinutes keeps recently used ref-0 grids; MaxGridsPerTick caps a call and the
// next call resumes at the same grid; the last-use stamp is written only with the switch on.
#include "../src/game/Maps/TerrainCleanupPolicy.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace
{
    int failures = 0;

    void Expect(uint64_t actual, uint64_t expected, char const* label)
    {
        if (actual != expected)
        {
            std::cerr << label << ": got " << actual << ", expected " << expected << "\n";
            ++failures;
        }
    }

    void ExpectTrue(bool value, char const* label)
    {
        if (!value)
        {
            std::cerr << label << ": expected true\n";
            ++failures;
        }
    }

    using terrain_cleanup::GRIDS_PER_SIDE;
    using terrain_cleanup::GRID_COUNT;

    struct Table
    {
        bool loaded[GRIDS_PER_SIDE][GRIDS_PER_SIDE] = {};
        int16_t ref[GRIDS_PER_SIDE][GRIDS_PER_SIDE] = {};
        uint32_t lastUse[GRIDS_PER_SIDE][GRIDS_PER_SIDE] = {};
    };

    // Deterministic pseudo-random table (no <random>: identical on every platform).
    Table MakeTable(uint32_t seed)
    {
        Table t;
        uint32_t s = seed;
        auto next = [&s]() { s = s * 1664525u + 1013904223u; return s >> 8; };
        for (uint32_t y = 0; y < GRIDS_PER_SIDE; ++y)
            for (uint32_t x = 0; x < GRIDS_PER_SIDE; ++x)
            {
                t.loaded[x][y] = (next() % 3) == 0;
                t.ref[x][y] = int16_t(next() % 4 == 0 ? next() % 3 : 0);
                t.lastUse[x][y] = next() % 4000;
            }
        return t;
    }

    // The loop CleanUpGrids ran before twow-repo#597 (y outer, x inner, free if loaded and ref 0).
    std::vector<std::pair<uint32_t, uint32_t>> LegacyPass(Table t)
    {
        std::vector<std::pair<uint32_t, uint32_t>> freed;
        for (int y = 0; y < int(GRIDS_PER_SIDE); ++y)
            for (int x = 0; x < int(GRIDS_PER_SIDE); ++x)
            {
                int16_t const& iRef = t.ref[x][y];
                if (t.loaded[x][y] && iRef == 0)
                {
                    t.loaded[x][y] = false;
                    freed.emplace_back(uint32_t(x), uint32_t(y));
                }
            }
        return freed;
    }

    terrain_cleanup::PassResult Pass(Table& t, uint32_t cursor, uint32_t idleMinutes, uint32_t maxPerTick, uint32_t nowSec,
        std::vector<std::pair<uint32_t, uint32_t>>& freed, uint32_t* examined = nullptr)
    {
        return terrain_cleanup::RunPass(cursor, maxPerTick,
            [&](uint32_t x, uint32_t y)
            {
                if (examined)
                    ++*examined;
                return terrain_cleanup::ShouldFree(t.loaded[x][y], t.ref[x][y], idleMinutes, nowSec, t.lastUse[x][y]);
            },
            [&](uint32_t x, uint32_t y)
            {
                t.loaded[x][y] = false;
                freed.emplace_back(x, y);
            });
    }
}

int main()
{
    // 1. Off path: identical decisions, identical order, one call, every grid examined once.
    for (uint32_t seed : { 1u, 7u, 597u, 0xC0FFEEu })
    {
        Table t = MakeTable(seed);
        std::vector<std::pair<uint32_t, uint32_t>> const expected = LegacyPass(t);
        std::vector<std::pair<uint32_t, uint32_t>> freed;
        uint32_t examined = 0;
        terrain_cleanup::PassResult const r = Pass(t, 0, 0, 0, 123456, freed, &examined);
        ExpectTrue(freed == expected, "off: same grids in the same order as the old loop");
        ExpectTrue(!expected.empty(), "off: the table has freeable grids");
        Expect(r.freed, expected.size(), "off: freed count");
        ExpectTrue(r.done, "off: one call is a full pass");
        Expect(r.nextCursor, 0, "off: cursor back at 0");
        Expect(examined, GRID_COUNT, "off: every grid examined once");
    }

    // 2. ShouldFree: today's rule with IdleMinutes 0, regardless of the stamps.
    ExpectTrue(terrain_cleanup::ShouldFree(true, 0, 0, 0, 0), "off: loaded ref 0 freed");
    ExpectTrue(terrain_cleanup::ShouldFree(true, 0, 0, 5, 5), "off: freed even if just used");
    ExpectTrue(!terrain_cleanup::ShouldFree(true, 1, 0, 0, 0), "off: referenced kept");
    ExpectTrue(!terrain_cleanup::ShouldFree(false, 0, 0, 0, 0), "off: not loaded skipped");

    // 3. IdleMinutes: kept while used within the window, freed at and after the edge; refs still win.
    {
        uint32_t const now = 10000;
        ExpectTrue(!terrain_cleanup::ShouldFree(true, 0, 10, now, now), "idle 10: used now kept");
        ExpectTrue(!terrain_cleanup::ShouldFree(true, 0, 10, now, now - 599), "idle 10: 599 s kept");
        ExpectTrue(terrain_cleanup::ShouldFree(true, 0, 10, now, now - 600), "idle 10: 600 s freed (>=)");
        ExpectTrue(terrain_cleanup::ShouldFree(true, 0, 10, now, now - 601), "idle 10: 601 s freed");
        ExpectTrue(!terrain_cleanup::ShouldFree(true, 2, 10, now, 0), "idle 10: referenced kept");
        ExpectTrue(!terrain_cleanup::ShouldFree(false, 0, 10, now, 0), "idle 10: not loaded skipped");
        // the terrain clock wrapped: last use 0xFFFFFFF0, now 5 -> 21 s idle
        Expect(terrain_cleanup::IdleSeconds(5, 0xFFFFFFF0u), 21, "idle: wrap-around");
        ExpectTrue(!terrain_cleanup::ShouldFree(true, 0, 1, 5, 0xFFFFFFF0u), "idle 1: 21 s after a wrap kept");
        // above the bound the window is one day, not a uint32 overflow of minutes * 60
        Expect(terrain_cleanup::ClampIdleMinutes(0), 0, "clamp: 0 stays off");
        Expect(terrain_cleanup::ClampIdleMinutes(1440), 1440, "clamp: 1440 kept");
        Expect(terrain_cleanup::ClampIdleMinutes(0xFFFFFFFFu), terrain_cleanup::MAX_IDLE_MINUTES, "clamp: huge -> 1440");
        ExpectTrue(!terrain_cleanup::ShouldFree(true, 0, 0xFFFFFFFFu, 86399, 0), "idle huge: 86399 s kept");
        ExpectTrue(terrain_cleanup::ShouldFree(true, 0, 0xFFFFFFFFu, 86400, 0), "idle huge: 86400 s freed");
    }

    // 4. IdleMinutes in a pass: only the stale ref-0 grids go.
    {
        Table t = MakeTable(42);
        uint32_t const now = 4000;  // lastUse in [0, 4000): idle = now - lastUse in (0, 4000]
        std::vector<std::pair<uint32_t, uint32_t>> freed;
        terrain_cleanup::PassResult const r = Pass(t, 0, 30, 0, now, freed);  // 1800 s
        ExpectTrue(r.done, "idle pass: one call");
        uint32_t expected = 0;
        Table const fresh = MakeTable(42);
        for (uint32_t y = 0; y < GRIDS_PER_SIDE; ++y)
            for (uint32_t x = 0; x < GRIDS_PER_SIDE; ++x)
                if (fresh.loaded[x][y] && fresh.ref[x][y] == 0 && now - fresh.lastUse[x][y] >= 1800)
                    ++expected;
        Expect(r.freed, expected, "idle pass: stale ref-0 grids only");
        ExpectTrue(r.freed < LegacyPass(fresh).size(), "idle pass: fewer than off");
        for (auto const& g : freed)
            ExpectTrue(now - fresh.lastUse[g.first][g.second] >= 1800, "idle pass: every freed grid was idle");
    }

    // 5. MaxGridsPerTick: capped calls, resumed at the same grid, same set and order as one full pass.
    {
        Table t = MakeTable(7);
        std::vector<std::pair<uint32_t, uint32_t>> const expected = LegacyPass(t);
        std::vector<std::pair<uint32_t, uint32_t>> freed;
        uint32_t cursor = 0;
        uint32_t calls = 0;
        bool done = false;
        while (!done && calls < GRID_COUNT)
        {
            std::vector<std::pair<uint32_t, uint32_t>> step;
            terrain_cleanup::PassResult const r = Pass(t, cursor, 0, 3, 0, step);
            ExpectTrue(r.freed <= 3, "cap 3: at most 3 per call");
            ExpectTrue(r.done || r.freed == 3, "cap 3: an open pass stopped at the cap");
            freed.insert(freed.end(), step.begin(), step.end());
            cursor = r.nextCursor;
            done = r.done;
            ++calls;
        }
        ExpectTrue(done, "cap 3: the pass finishes");
        ExpectTrue(freed == expected, "cap 3: same grids in the same order as one full pass");
        // ceil(n / 3) calls, one more when the cap was hit before the end of the table
        ExpectTrue(calls >= (expected.size() + 2) / 3 && calls <= expected.size() / 3 + 1, "cap 3: calls");
    }

    // 6. Cap edges: cap 1 on a single freeable grid at the last index finishes in one call; an open
    // pass resumes at the first unexamined grid; an out-of-range cursor starts over.
    {
        Table t;
        t.loaded[GRIDS_PER_SIDE - 1][GRIDS_PER_SIDE - 1] = true;
        std::vector<std::pair<uint32_t, uint32_t>> freed;
        terrain_cleanup::PassResult const r = Pass(t, 0, 0, 1, 0, freed);
        Expect(r.freed, 1, "cap 1, last grid: freed");
        ExpectTrue(r.done, "cap 1, last grid: done in one call");

        Table u;
        u.loaded[0][0] = true;   // index 0
        u.loaded[5][0] = true;   // index 5
        u.loaded[1][2] = true;   // index 2 * 64 + 1 = 129
        std::vector<std::pair<uint32_t, uint32_t>> f1;
        terrain_cleanup::PassResult const a = Pass(u, 0, 0, 1, 0, f1);
        ExpectTrue(!a.done, "cap 1: open after the first grid");
        Expect(a.nextCursor, 1, "cap 1: resumes after index 0");
        terrain_cleanup::PassResult const b = Pass(u, a.nextCursor, 0, 1, 0, f1);
        Expect(b.nextCursor, 6, "cap 1: resumes after index 5");
        terrain_cleanup::PassResult const c = Pass(u, b.nextCursor, 0, 1, 0, f1);
        ExpectTrue(!c.done && c.nextCursor == 130, "cap 1: stops right after index 129");
        terrain_cleanup::PassResult const d = Pass(u, c.nextCursor, 0, 1, 0, f1);
        ExpectTrue(d.done && d.freed == 0, "cap 1: last call finds nothing and closes the pass");
        Expect(f1.size(), 3, "cap 1: all three freed");

        Table v;
        v.loaded[3][0] = true;
        std::vector<std::pair<uint32_t, uint32_t>> f2;
        terrain_cleanup::PassResult const e = Pass(v, GRID_COUNT + 9, 0, 0, 0, f2);
        ExpectTrue(e.done && e.freed == 1, "cursor out of range: full pass from 0");
    }

    // 7. Budget, scan and stamp gates.
    ExpectTrue(terrain_cleanup::BudgetLeft(0, 1000000), "budget: 0 = unlimited");
    ExpectTrue(terrain_cleanup::BudgetLeft(2, 1), "budget: 1 of 2");
    ExpectTrue(!terrain_cleanup::BudgetLeft(2, 2), "budget: 2 of 2 spent");
    ExpectTrue(terrain_cleanup::ScanThisTick(true, false), "scan: timer passed");
    ExpectTrue(terrain_cleanup::ScanThisTick(false, true), "scan: open pass resumes");
    ExpectTrue(!terrain_cleanup::ScanThisTick(false, false), "scan: idle tick (today)");
    ExpectTrue(!terrain_cleanup::StampEnabled(0), "stamp: off with IdleMinutes 0");
    ExpectTrue(terrain_cleanup::StampEnabled(1), "stamp: on with IdleMinutes 1");
    ExpectTrue(terrain_cleanup::NeedsStamp(4, 5), "stamp: new second stored");
    ExpectTrue(!terrain_cleanup::NeedsStamp(5, 5), "stamp: same second not stored again");

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "terrain_cleanup_597_test: OK\n";
    return 0;
}
