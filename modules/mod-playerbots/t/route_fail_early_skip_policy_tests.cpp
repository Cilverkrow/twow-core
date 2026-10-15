#include "StallGuardPolicy.h"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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
}

int main()
{
    using namespace ai::stall_guard;

    // twow-repo#592 (PATH-02): level clamped to 0..2.
    Require(ClampRouteFailEarlySkip(-1) == 0 && ClampRouteFailEarlySkip(0) == 0, "0 and below = off");
    Require(ClampRouteFailEarlySkip(1) == 1 && ClampRouteFailEarlySkip(2) == 2, "shadow and active kept");
    Require(ClampRouteFailEarlySkip(7) == RouteFailEarlySkipMax, "clamped to 2");

    float const minGap = 25.0f + EarlySkipGapMargin;   // spell distance 25 + 5
    RouteFailOrigin const stored = MakeRouteFailOrigin(1, 1000.0f, -4000.0f, 50.0f, 1100.0f, -4100.0f, 30.0f, 80.0f);
    auto now = [](float sx, float sy, float sz, float tx, float ty, float tz) {
        return MakeRouteFailOrigin(1, sx, sy, sz, tx, ty, tz, -1.0f);
    };

    // Start and exact target each within 1 yd (3D): the same direct attempt.
    Require(EarlySkipMatches(stored, now(1000.0f, -4000.0f, 50.0f, 1100.0f, -4100.0f, 30.0f), minGap), "same start and target");
    Require(EarlySkipMatches(stored, now(1000.9f, -4000.0f, 50.0f, 1100.0f, -4100.0f, 30.0f), minGap), "start 0.9 yd away");
    Require(!EarlySkipMatches(stored, now(1001.1f, -4000.0f, 50.0f, 1100.0f, -4100.0f, 30.0f), minGap), "start 1.1 yd away");
    Require(!EarlySkipMatches(stored, now(1000.0f, -4000.0f, 51.1f, 1100.0f, -4100.0f, 30.0f), minGap), "start 1.1 yd higher");
    Require(EarlySkipMatches(stored, now(1000.0f, -4000.0f, 50.0f, 1100.0f, -4099.1f, 30.0f), minGap), "target 0.9 yd away");
    Require(!EarlySkipMatches(stored, now(1000.0f, -4000.0f, 50.0f, 1100.0f, -4098.9f, 30.0f), minGap), "target 1.1 yd away");
    Require(!EarlySkipMatches(stored, now(1000.0f, -4000.0f, 50.0f, 1100.0f, -4100.0f, 31.1f), minGap), "target 1.1 yd higher");
    {
        RouteFailOrigin otherMap = now(1000.0f, -4000.0f, 50.0f, 1100.0f, -4100.0f, 30.0f);
        otherMap.startMap = 0;
        Require(!EarlySkipMatches(stored, otherMap, minGap), "start on another map");
    }

    // The stored direct attempt must have ended far from its goal (or had no path at all).
    {
        RouteFailOrigin nearGoal = stored;
        nearGoal.directGap = 29.9f;
        Require(!EarlySkipMatches(nearGoal, stored, minGap), "direct gap below spell distance + 5: no early skip");
        nearGoal.directGap = 30.0f;
        Require(EarlySkipMatches(nearGoal, stored, minGap), "direct gap at spell distance + 5");
        nearGoal.directGap = -1.0f;
        Require(EarlySkipMatches(nearGoal, stored, minGap), "no direct path at all (other map)");
    }

    // Unknown origin (budget stop, getRoute callers without a direct attempt): never.
    Require(!EarlySkipMatches(RouteFailOrigin(), stored, minGap), "unknown origin never matches");

    // The store: only while the cooldown runs, only for a block that kept its origin.
    {
        RouteCooldownStore store;
        std::uint64_t const key = RouteCooldownStore::Key(4485, 1, 1100.0f, -4100.0f);
        float gap = 0.0f;
        store.Block(key, 160, 100, stored);
        Require(store.IsBlocked(key, 159), "blocked as before");
        Require(store.EarlySkip(key, 159, stored, minGap, gap) && gap == 80.0f, "match during the cooldown, stored gap returned");
        Require(!store.EarlySkip(key, 160, stored, minGap, gap), "no match after the cooldown");
        store.Block(key, 400, 100);
        Require(store.IsBlocked(key, 399) && !store.EarlySkip(key, 200, stored, minGap, gap), "a budget block (no origin) replaces it: never");
        Require(!store.EarlySkip(RouteCooldownStore::Key(4486, 1, 1100.0f, -4100.0f), 101, stored, minGap, gap), "other bot: no entry");
    }

    // Still bounded with origins under concurrency.
    {
        RouteCooldownStore shared;
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&shared, &stored, t]() {
                for (int i = 0; i < 3000; ++i)
                {
                    std::uint64_t const k = RouteCooldownStore::Key(std::uint32_t(t * 100000 + i), 0, float(i), 0.0f);
                    shared.Block(k, 1000, 10, stored);
                    float gap = 0.0f;
                    shared.EarlySkip(k, 11, stored, 30.0f, gap);
                }
            });
        for (std::thread& thread : threads)
            thread.join();
        Require(shared.Size() <= RouteCooldownStore::MaxEntries, "store stays bounded");
    }

    std::cout << "route_fail_early_skip policy tests passed\n";
    return 0;
}
