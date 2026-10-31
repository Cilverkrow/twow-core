// twow-repo#653 (OB-00 31.10.2026): AiPlayerbot.ActionLog.DamageSampleEvery - the action log's damage lines every Nth event of a
// bot, dealt and taken counted separately; 1 = every event, below 1 = the default 5, at most 100.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "DamageLogPolicy.h"

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
    using namespace ai::damage_log;

    Require(Every(5) == 5 && Every(1) == 1 && Every(17) == 17, "the configured rate");
    Require(Every(0) == DefaultEvery && Every(-3) == DefaultEvery, "below 1: the default 5 (as before)");
    Require(Every(1000) == MaxEvery, "at most 100");
    Require(DefaultEvery == 5 && MaxEvery == 100, "constants");

    // Rate 5: the 5th, 10th, ... event; rate 1: every event.
    Require(!Due(1, 5) && !Due(4, 5) && Due(5, 5) && !Due(6, 5) && Due(10, 5), "every 5th event");
    Require(Due(1, 1) && Due(2, 1) && Due(3, 1), "rate 1: every event");
    unsigned written = 0;
    for (std::uint32_t n = 1; n <= 100; ++n)
        written += Due(n, 5) ? 1u : 0u;
    Require(written == 20, "20 of 100 events at rate 5");

    std::cout << "damage_log_policy tests passed\n";
    return 0;
}
