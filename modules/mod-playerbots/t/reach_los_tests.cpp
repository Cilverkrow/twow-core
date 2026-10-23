// twow-repo#594 (AiPlayerbot.Rotation.ReachLosClose): ReachLosPolicy.h.
#include <cstdlib>
#include <iostream>

#include "ReachLosPolicy.h"

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

bool Near(float a, float b)
{
    return a - b < 0.001f && b - a < 0.001f;
}
}

int main()
{
    using ai::reach_los::ChaseDistance;
    float const preferred = 30.0f * 0.75f;   // 30 yd spell range

    // In sight: always 75 % of the range, switch or not.
    Require(Near(ChaseDistance(true, false, false, 20.7f, preferred), preferred), "in LOS, hostile, off");
    Require(Near(ChaseDistance(true, false, true, 20.7f, preferred), preferred), "in LOS, hostile, on");
    // Friendly out of sight: unchanged 0.9 rule.
    Require(Near(ChaseDistance(false, true, false, 20.0f, preferred), 18.0f), "friend out of LOS: distance x 0.9");
    Require(Near(ChaseDistance(false, true, true, 20.0f, preferred), 18.0f), "friend out of LOS, switch on: unchanged");
    // The Linalella case: hostile, 20.7 yd, out of sight.
    Require(Near(ChaseDistance(false, false, false, 20.7f, preferred), preferred), "off: 22.5 yd - the bot at 20.7 yd never moves");
    Require(Near(ChaseDistance(false, false, true, 20.7f, preferred), 20.7f * 0.5f), "on: half the distance, the bot moves");
    Require(ChaseDistance(false, false, true, 20.7f, preferred) < 20.7f, "on: closer than the bot stands");
    // Far away and out of sight: never farther than 75 % of the range.
    Require(Near(ChaseDistance(false, false, true, 100.0f, preferred), preferred), "on, far: capped at preferred");

    std::cout << "reach_los_tests passed\n";
    return 0;
}
