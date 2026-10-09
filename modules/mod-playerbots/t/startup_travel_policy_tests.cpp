#include "StartupTravelPolicy.h"

#include <cstdlib>
#include <iostream>
#include <set>

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
    using namespace ai::startup_travel;

    // Jitter: within 0..max, off at 0, spread over the window for a roster-sized guid set.
    Require(JitterSeconds(1234, 0) == 0, "jitter off");
    std::set<std::uint32_t> minutes;
    std::uint32_t maxSeen = 0;
    for (std::uint32_t guid = 1; guid <= 180; ++guid)
    {
        std::uint32_t const j = JitterSeconds(guid * 37 + 11, 300);
        Require(j <= 300, "jitter within 0..300");
        maxSeen = j > maxSeen ? j : maxSeen;
        minutes.insert(j / 60);
    }
    Require(minutes.size() >= 5 && maxSeen >= 240, "180 bots spread over all five minutes");
    Require(!JitterDone(1000, 1000, 7, 300) || JitterSeconds(7, 300) == 0, "first journey waits for its jitter");
    Require(JitterDone(1000 + 300, 1000, 7, 300), "after the window every bot may start");

    // Window: only in the first WindowSeconds of uptime, off at 0.
    Require(InWindow(10, 900) && !InWindow(900, 900) && !InWindow(10, 0), "startup window");

    // Long move: another map or far, unless a cached route already ends at the target.
    Require(IsLongMove(false, 10.0f, false), "other map: long");
    Require(IsLongMove(true, 500.0f, false), "far on the same map: long");
    Require(!IsLongMove(true, 50.0f, false), "near: short");
    Require(!IsLongMove(true, 500.0f, true), "cached route: no new path to build");

    // Budget: at most max per 100 ms slot, new slot refills, 0 = unlimited.
    LongMoveBudget budget;
    Require(budget.TryTake(1000, 2) && budget.TryTake(1050, 2) && !budget.TryTake(1099, 2), "two per slot");
    Require(budget.TryTake(1100, 2), "next slot refills");
    LongMoveBudget unlimited;
    for (int i = 0; i < 50; ++i)
        Require(unlimited.TryTake(5000, 0), "max 0 = no cap");

    // One log per minute.
    Counters counters;
    Require(counters.LogDue(120) && !counters.LogDue(150) && counters.LogDue(180), "log once per minute");

    // twow-repo#541 (b): the runtime budget is its own instance - using up one does not touch the other.
    Require(&RuntimeBudget() != &SharedBudget(), "separate budget instances");
    Require(SharedBudget().TryTake(9000, 1) && !SharedBudget().TryTake(9010, 1), "startup budget used up");
    Require(RuntimeBudget().TryTake(9020, 1), "runtime budget still free in the same slot");
    Require(!RuntimeBudget().TryTake(9030, 1) && RuntimeBudget().TryTake(9100, 1), "runtime budget: one per slot, refilled");
    RuntimeBudgetCounters runtimeCounters;
    Require(runtimeCounters.LogDue(600) && !runtimeCounters.LogDue(659) && runtimeCounters.LogDue(660), "runtime line once per minute");

    std::cout << "startup_travel_policy_tests passed\n";
    return 0;
}
