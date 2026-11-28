#include "GrindCrowdPolicy.h"

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
    using namespace ai::grind_crowd;

    // Off: the old behaviour - the penalty replaces the distance.
    Require(CrowdedDistance(80.0f, 2, false) == 10.0f, "off: 2 bots, score is 10 whatever the distance");
    Require(CrowdedDistance(3.0f, 2, false) == 10.0f, "off: a near candidate gets the same 10");

    // On: the penalty is added.
    Require(CrowdedDistance(80.0f, 2, true) == 90.0f, "on: 80 yd + 2 x 5");
    Require(CrowdedDistance(3.0f, 4, true) == 23.0f, "on: 3 yd + 4 x 5");
    Require(CrowdedDistance(30.0f, 0, true) == 30.0f, "on: nobody targets, distance kept");

    // The bug in one comparison: a crowded creature far away must not beat a free one nearer.
    float const free = 40.0f;
    Require(CrowdedDistance(80.0f, 1, false) < free, "off: the far crowded creature wins (the bug)");
    Require(CrowdedDistance(80.0f, 1, true) > free, "on: the nearer free creature wins");
    return 0;
}
