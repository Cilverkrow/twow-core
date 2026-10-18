// twow-repo#587: RescueLimiter::HasCapacity checks for a free slot without taking it.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestSearchPolicy.h"

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
    using namespace ai::quest_search;
    RescueLimiter limiter;
    std::uint32_t const t = 100000;

    Require(limiter.HasCapacity(t), "empty: capacity");
    Require(limiter.HasCapacity(t) && limiter.recent.empty(), "a peek takes no slot");
    for (std::uint32_t i = 0; i < RescueGlobalMax; ++i)
        Require(limiter.TryAcquire(t + i), "the global limit");
    Require(!limiter.HasCapacity(t + 10) && !limiter.TryAcquire(t + 10), "full: no capacity, no slot");
    // The window moves on: the first slot is free again after RescueGlobalWindowSeconds.
    Require(limiter.HasCapacity(t + RescueGlobalWindowSeconds), "window over: capacity again");
    Require(limiter.TryAcquire(t + RescueGlobalWindowSeconds), "and a slot");

    std::cout << "rescue_limiter_capacity_tests passed\n";
    return 0;
}
