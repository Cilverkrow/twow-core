#include "AiJitterPolicy.h"

#include <cstdlib>
#include <iostream>

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
    using namespace ai::jitter;

    // Off and edge cases.
    Require(JitteredDelay(1500, 0, 0) == 1500, "pct 0 is off");
    Require(JitteredDelay(0, 15, 1000) == 0, "a zero delay stays zero");

    // +-15 % of the global cooldown.
    Require(JitteredDelay(1500, 15, 0) == 1275, "roll 0 -> -15 %");
    Require(JitteredDelay(1500, 15, RollRange / 2) == 1500, "middle roll -> unchanged");
    Require(JitteredDelay(1500, 15, RollRange) == 1725, "roll max -> +15 %");
    Require(JitteredDelay(1500, 15, 5000) == 1725, "rolls past the range are clamped");
    Require(JitteredDelay(1000, 90, 0) == 500, "pct is capped at 50");

    // Symmetric: the mean over all rolls stays the delay.
    for (std::uint32_t delay : {100u, 1500u, 5000u, 10000u})
    {
        std::uint64_t sum = 0;
        std::uint32_t lo = ~0u, hi = 0;
        for (std::uint32_t roll = 0; roll <= RollRange; ++roll)
        {
            std::uint32_t const d = JitteredDelay(delay, 15, roll);
            sum += d;
            lo = d < lo ? d : lo;
            hi = d > hi ? d : hi;
        }
        double const mean = double(sum) / double(RollRange + 1);
        Require(mean > delay * 0.99 && mean < delay * 1.01, "mean unchanged (within integer rounding)");
        Require(lo >= delay - delay * 15 / 100 && hi <= delay + delay * 15 / 100, "within +-15 %");
    }

    // Parked spread: the first update lands within one interval, the stamp is never 0 ("now").
    Require(SpreadLastUpdate(50000, 10000, 0) == 50000, "roll 0 -> due after a full interval");
    Require(SpreadLastUpdate(50000, 10000, RollRange) == 40000, "roll max -> due now");
    Require(SpreadLastUpdate(50000, 10000, RollRange / 2) == 45000, "middle -> half an interval");
    Require(SpreadLastUpdate(10000, 10000, RollRange) == 1, "never 0");
    Require(SpreadLastUpdate(5, 10000, RollRange) != 0, "wraps like the ms clock, never 0");

    // Start offset after login: [0, max].
    Require(StartOffset(3000, 0) == 0 && StartOffset(3000, RollRange) == 3000, "start offset range");
    Require(StartOffset(3000, RollRange / 2) == 1500, "start offset middle");

    std::cout << "ai_jitter_policy_tests passed\n";
    return 0;
}
