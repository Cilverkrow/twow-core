#include "FarMovePolicy.h"
#include "LevelGuardPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

    // twow-repo#601 (OB-00 28.10.2026): the wait state for the activity trace - none without a far move, waiting up to the grace
    // after its time (Arrive runs at the next MoveTo2 pass), overdue later.
    Require(WaitStateOf(1000, 0, true) == WaitState::None, "no far move");
    Require(WaitStateOf(1000, 1240, true) == WaitState::Waiting && WaitStateOf(1240, 1240, true) == WaitState::Waiting,
        "waiting up to its time");
    Require(WaitStateOf(1240 + ArriveGraceSeconds, 1240, true) == WaitState::Waiting, "and up to the grace after it");
    Require(WaitStateOf(1240 + ArriveGraceSeconds + 1, 1240, true) == WaitState::Overdue, "overdue after the grace");
    Require(WaitStateOf(1000, 1240, false) == WaitState::None && WaitStateOf(2000, 1240, false) == WaitState::None,
        "a far move to another target than the travel target (abandoned, 8.33b keeps its time): none");
    Require(ArriveGraceSeconds > 10, "the grace covers the wait's longest duration (10 s)");
    Require(std::string(WaitStateName(WaitState::None)) == "none" && std::string(WaitStateName(WaitState::Waiting)) == "waiting" &&
        std::string(WaitStateName(WaitState::Overdue)) == "overdue", "names");

    // twow-repo#601 cand4-B (RosterFarMove.ArriveSafe): the core's proximity aggro at the arrival point, without LOS.
    Require(Threatens(20.0f, 18.0f, 0.0f, false), "inside the aggro radius plus the margin (18 + 5)");
    Require(Threatens(23.0f, 18.0f, 2.9f, false), "at the margin edge, within the z range");
    Require(!Threatens(23.1f, 18.0f, 0.0f, false), "beyond radius + margin");
    Require(!Threatens(10.0f, 18.0f, 3.5f, false), "a walker beyond its z range ignores the bot");
    Require(Threatens(10.0f, 18.0f, 3.5f, true), "a flier keeps its z range");
    Require(!Threatens(1.0f, 0.0f, 0.0f, false), "aggro radius 0: never (aggro rate 0, disabled)");
    Require(ArriveSafeSearchYards >= 45.0f + ArriveSafeMarginYards, "the search covers the largest aggro radius");
    Require(ArriveSafeSteps * ArriveSafeStepYards == 120.0f, "at most 4 x 30 yd further back");
    // the height difference as the core counts it (GetDistanceZ: minus both bounding radii)
    Require(AggroDz(4.0f, 1.5f) == 2.5f && AggroDz(-4.0f, 1.5f) == 2.5f, "4 yd above or below minus 1.5 yd radii");
    Require(AggroDz(1.0f, 1.5f) == 0.0f, "never below 0");
    Require(Threatens(10.0f, 18.0f, AggroDz(4.0f, 1.5f), false), "a hillside mob 4 yd up still aggroes, as in the core");
    // the decline: 8.36 (until 0) until the target changes, ArriveSafe until its time
    Require(DeclineActive(0, 5000), "8.36 decline: no time limit");
    Require(DeclineActive(1120, 1000) && !DeclineActive(1120, 1120), "ArriveSafe decline lapses at its time");
    Require(ArriveSafeDeclineSeconds == 120, "two minutes for the walk of up to 180 yd");

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
        Require(ch.outcome == ArriveOutcome::Unsafe && ch.index == 4, "none free: the fewest threats, the farthest of equal ones");
        c[0] = { true, 1 }; c[1] = { true, 4 }; c[2] = { true, 5 };
        ch = ChooseArrival(c, 3);
        Require(ch.outcome == ArriveOutcome::Unsafe && ch.index == 0, "a line across a bigger camp: not into it");
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

    // twow-repo#601 cand4 lever (ii) (LevelGuardPolicy.h): the rule, the side mask, the clamps and the spawn grid.
    {
        using namespace ai::level_guard;
        Require(Fires(13, 10, 3) && Fires(20, 10, 3) && !Fires(12, 10, 3), "level_max >= bot + margin fires, one below does not");
        Require(HostileTo(MaskPlayer, MaskHorde) && HostileTo(MaskAlliance, MaskAlliance) && !HostileTo(MaskAlliance, MaskHorde),
            "the player bit or the bot's side bit");
        Require(!HostileTo(0, MaskHorde), "a neutral spawn (mask 0) never counts");
        Require(ClampMargin(3) == 3 && ClampMargin(0) == DefaultMargin && ClampMargin(11) == DefaultMargin, "margin 1..10, else the default");
        Require(ClampRadius(50.0f) == 50.0f && ClampRadius(5.0f) == DefaultRadius && ClampRadius(150.0f) == DefaultRadius,
            "radius 10..100, else the default");

        Grid grid;
        auto spawn = [](float x, float y, uint32_t entry, uint8_t level, uint8_t mask, uint8_t rank = 0)
        {
            Spawn s; s.x = x; s.y = y; s.entry = entry; s.levelMax = level; s.hostileMask = mask; s.rank = rank; return s;
        };
        grid.Add(1, spawn(100.0f, 100.0f, 501, 14, MaskPlayer));          // 0 yd: level 14
        grid.Add(1, spawn(140.0f, 100.0f, 502, 18, MaskPlayer, 1));       // 40 yd, elite: level 18
        grid.Add(1, spawn(170.0f, 100.0f, 503, 30, MaskPlayer));          // 70 yd: outside 50
        grid.Add(1, spawn(100.0f, 130.0f, 504, 25, MaskAlliance));        // 30 yd, hostile to the alliance only
        grid.Add(0, spawn(100.0f, 100.0f, 505, 40, MaskPlayer));          // other map
        Require(grid.Count() == 5, "five spawns");

        Hit h = grid.Query(1, 100.0f, 100.0f, 50.0f, 12, 3, MaskHorde);
        Require(h.fired && h.entry == 502 && h.level == 18 && h.rank == 1 && h.hostile == 2,
            "horde L12: the highest firing spawn within 50 yd (the alliance-only one and the 70 yd one left out)");
        Require(std::fabs(h.dist - 40.0f) < 0.01f, "its distance");
        h = grid.Query(1, 100.0f, 100.0f, 50.0f, 12, 3, MaskAlliance);
        Require(h.fired && h.entry == 504 && h.hostile == 3, "alliance L12: the alliance-hostile L25 counts too");
        h = grid.Query(1, 100.0f, 100.0f, 50.0f, 16, 3, MaskHorde);
        Require(!h.fired && h.entry == 502 && h.hostile == 2, "horde L16: nothing >= 19 - not fired, the highest hostile for the log");
        h = grid.Query(1, 100.0f, 100.0f, 80.0f, 16, 3, MaskHorde);
        Require(h.fired && h.entry == 503, "radius 80 reaches the L30 at 70 yd");
        h = grid.Query(1, 1000.0f, 1000.0f, 50.0f, 1, 1, MaskHorde);
        Require(!h.fired && h.hostile == 0 && h.entry == 0, "an empty area: nothing");

        // negative coordinates and a query across cell borders (cells of 50 yd from -MapHalf)
        Grid g2;
        g2.Add(0, spawn(-8913.0f, -120.0f, 600, 9, MaskPlayer));
        Require(g2.Query(0, -8949.0f, -120.0f, 50.0f, 5, 3, MaskAlliance).fired, "a spawn 36 yd away in the neighbouring cell");
        Require(Grid::Cell(-MapHalf) == 0 && Grid::Cell(MapHalf - 1.0f) == int32_t((2.0f * MapHalf - 1.0f) / CellSize),
            "cells from 0 at -MapHalf");
    }

    std::cout << "far_move_policy_tests passed\n";
    return 0;
}
