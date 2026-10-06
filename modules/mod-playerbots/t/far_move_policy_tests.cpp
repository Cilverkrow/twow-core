#include "FarMovePolicy.h"

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
    using namespace ai::far_move;

    // Walking time at run speed, capped below the turn-in stall window (300 s).
    Require(WaitSeconds(700.0f, 7.0f) == 100, "700 yd at 7 yd/s: 100 s");
    Require(WaitSeconds(5000.0f, 7.0f) == MaxWaitSeconds && MaxWaitSeconds < 300, "long trip capped below the stall window");
    Require(WaitSeconds(100.0f, 0.0f) == MaxWaitSeconds, "no speed: cap");

    // Start, wait, arrive; a new target restarts.
    Require(Next(1000, 0, false) == Step::Start, "nothing pending: start");
    Require(Next(1000, 1100, true) == Step::Wait, "before the walking time: wait");
    Require(Next(1100, 1100, true) == Step::Arrive, "walking time over: arrive");
    Require(Next(1100, 1100, false) == Step::Start, "other target: start again");
    Require(SameTargetYards >= 25.0f, "8.33b: small target shifts stay the same trip");

    // Hotfix 8.36: route watch (point to segment) and the arrival point before the target.
    Require(SegmentDistance({ 50.0f, 30.0f }, { 0.0f, 0.0f }, { 100.0f, 0.0f }) == 30.0f, "player beside the route: 30 yd");
    Require(SegmentDistance({ 150.0f, 0.0f }, { 0.0f, 0.0f }, { 100.0f, 0.0f }) == 50.0f, "player beyond the target end: distance to the end");
    Point2 const arrive = ArrivalPoint({ 0.0f, 0.0f }, { 1000.0f, 0.0f }, 60.0f);
    Require(arrive.x == 940.0f && arrive.y == 0.0f, "60 yd before the target on the line from the start");
    Point2 const near = ArrivalPoint({ 900.0f, 0.0f }, { 1000.0f, 0.0f }, 120.0f);
    Require(near.x == 900.0f, "route shorter than the back distance: the start");
    Point2 const off = ArrivalPoint({ 0.0f, 0.0f }, { 1000.0f, 0.0f }, 0.0f);
    Require(off.x == 1000.0f, "back distance 0: at the target (8.33 behaviour)");
    Require(ArriveAltYards > 60.0f, "the second arrival point lies further back");

    Counters counters;
    Require(counters.LogDue(120) && !counters.LogDue(130) && counters.LogDue(180), "one [FarMove] line a minute");

    std::cout << "far_move_policy_tests passed\n";
    return 0;
}
