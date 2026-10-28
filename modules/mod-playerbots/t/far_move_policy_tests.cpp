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

    // twow-repo#601 cand4-B (RosterFarMove.ArriveSafe): the core's proximity aggro at the arrival point, without LOS.
    Require(Threatens(20.0f, 18.0f, 0.0f, false), "inside the aggro radius plus the margin (18 + 5)");
    Require(Threatens(23.0f, 18.0f, 2.9f, false), "at the margin edge, within the z range");
    Require(!Threatens(23.1f, 18.0f, 0.0f, false), "beyond radius + margin");
    Require(!Threatens(10.0f, 18.0f, 3.5f, false), "a walker beyond its z range ignores the bot");
    Require(Threatens(10.0f, 18.0f, 3.5f, true), "a flier keeps its z range");
    Require(!Threatens(1.0f, 0.0f, 0.0f, false), "aggro radius 0: never (aggro rate 0, disabled)");
    Require(ArriveSafeSearchYards >= 45.0f + ArriveSafeMarginYards, "the search covers the largest aggro radius");
    Require(ArriveSafeSteps * ArriveSafeStepYards == 120.0f, "at most 4 x 30 yd further back");

    // The choice: the first free valid point; none free = the farthest valid one; none valid = keep the caller's point.
    {
        ArriveCandidate c[5];
        c[0] = { true, 0 };
        ArriveChoice ch = ChooseArrival(c, 1);
        Require(ch.outcome == ArriveOutcome::Kept && ch.index == 0, "first point free: kept");
        c[0] = { true, 3 }; c[1] = { false, 0 }; c[2] = { true, 0 };
        ch = ChooseArrival(c, 3);
        Require(ch.outcome == ArriveOutcome::Shifted && ch.index == 2, "an invalid step is skipped, the next free one taken");
        c[0] = { true, 3 }; c[1] = { true, 1 }; c[2] = { true, 2 }; c[3] = { false, 0 }; c[4] = { true, 1 };
        ch = ChooseArrival(c, 5);
        Require(ch.outcome == ArriveOutcome::Unsafe && ch.index == 4, "none free: the farthest valid point");
        c[0] = { true, 1 }; c[1] = { false, 0 }; c[2] = { false, 0 };
        ch = ChooseArrival(c, 3);
        Require(ch.outcome == ArriveOutcome::Unsafe && ch.index == 0, "only the first is valid and not free: it stays (unsafe)");
        c[0] = { false, 0 };
        ch = ChooseArrival(c, 1);
        Require(ch.outcome == ArriveOutcome::None && ch.index == 0, "nothing valid: none, the caller's point");
    }

    // The cost counters: sum and maximum.
    {
        ArriveSafeCounters s;
        s.NoteCost(40);
        s.NoteCost(120);
        s.NoteCost(80);
        Require(s.checkUs.load() == 240 && s.checkUsMax.load() == 120, "cost sum and maximum");
    }

    std::cout << "far_move_policy_tests passed\n";
    return 0;
}
