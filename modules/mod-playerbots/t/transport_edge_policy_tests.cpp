#include "TransportEdgePolicy.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// twow-repo#342 (S1, AiPlayerbot.Path.TransportEdgeTrace): edge key, run cache, episode state machine, line formats, counters.
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

using namespace ai::transport_edge;

PathNode N(std::uint32_t map, float x, float y, std::uint32_t delay = 0, float z = 0.0f)
{
    PathNode n;
    n.map = map;
    n.x = x;
    n.y = y;
    n.z = z;
    n.delay = delay;
    return n;
}

Key K(std::uint32_t ride, std::uint32_t fromMap, float fx, float fy, std::uint32_t toMap, float tx, float ty)
{
    Key k;
    k.valid = true;
    k.ride = ride;
    k.fromMap = fromMap;
    k.fromX = fx;
    k.fromY = fy;
    k.toMap = toMap;
    k.toX = tx;
    k.toY = ty;
    return k;
}

RunPoint P(bool transport, std::uint32_t entry, std::uint32_t map, float x, float y)
{
    RunPoint p;
    p.transport = transport;
    p.entry = entry;
    p.map = map;
    p.x = x;
    p.y = y;
    return p;
}

bool Has(std::string const& s, char const* part) { return s.find(part) != std::string::npos; }

// Wait ticks every 60 s in (from, to): all silent (no gap; the caller keeps the timeout outside the range).
void SilentTicks(State& s, Key const& key, std::uint32_t from, std::uint32_t to, char const* message)
{
    for (std::uint32_t t = from + 60000; t < to; t += 60000)
        Require(!OnWait(s, key, t, WaitTimeoutMs).Any(), message);
}
}

int main()
{
    // Fixed trace thresholds (map section 3; the shared TransportWaitTimeoutMin key is S3b's).
    Require(WaitTimeoutMs == 900000u, "wait_timeout / overrun at 900 s");
    Require(GapMs == 300000u, "episode gap 300 s (S0 parser)");
    Require(LinesPerMinute == 120u, "own budget 120 lines/min");

    // Two-stop TaxiPath (Rut'theran -> Auberdine shape): A = first delay>0 stop in point order.
    std::vector<PathNode> const ship = {
        N(1, 8550.0f, 1020.0f, 30000), N(1, 8000.0f, 900.0f), N(1, 7000.0f, 500.0f), N(1, 6580.0f, 770.0f, 30000),
        N(1, 7000.0f, 700.0f), N(1, 8000.0f, 1000.0f),
    };
    {
        Key ab = K(176244, 1, 8546.0f, 1023.0f, 1, 6575.0f, 768.0f);   // dock node ~5 yd from stop A
        Derive(ab, ship, 30.0f);
        Require(ab.stops == 2 && ab.dirFrom == 0 && ab.dirTo == 1, "2 stops: from A to B");
        Require(DirName(ab) == "AB" && EdgeName(ab) == "go:176244:AB", "2 stops: AB");
        Require(ab.rideS > 0.0f, "ride seconds from the TaxiPath length / moveSpeed");
        float const len = std::sqrt(550.0f * 550.0f + 120.0f * 120.0f) + std::sqrt(1000.0f * 1000.0f + 400.0f * 400.0f) +
            std::sqrt(420.0f * 420.0f + 270.0f * 270.0f);
        Require(std::fabs(ab.rideS - len / 30.0f) < 0.01f, "ride_s_formula = path length A..B / speed");

        Key ba = K(176244, 1, 6575.0f, 768.0f, 1, 8546.0f, 1023.0f);
        Derive(ba, ship, 30.0f);
        Require(DirName(ba) == "BA" && EdgeName(ba) == "go:176244:BA", "2 stops: BA");
        Require(ba.rideS > 0.0f && std::fabs(ba.rideS - ab.rideS) > 0.01f, "BA wraps around the path end");

        Key noSpeed = ab;
        Derive(noSpeed, ship, 0.0f);
        Require(noSpeed.rideS == -1.0f && DirName(noSpeed) == "AB", "no move speed: ride seconds unknown, direction kept");
    }

    // Three stops: <i>-<j>, ordinals from 1 in TaxiPath order.
    {
        std::vector<PathNode> const tri = {
            N(0, 0.0f, 0.0f, 1000), N(0, 500.0f, 0.0f), N(0, 1000.0f, 0.0f, 1000), N(0, 1500.0f, 0.0f), N(0, 2000.0f, 0.0f, 1000),
        };
        Key k = K(5, 0, 1002.0f, 3.0f, 0, 1998.0f, -2.0f);
        Derive(k, tri, 10.0f);
        Require(k.stops == 3 && DirName(k) == "2-3" && EdgeName(k) == "go:5:2-3", "3 stops: i-j, ordinals from 1");
        Require(std::fabs(k.rideS - 100.0f) < 0.01f, "3 stops: ride seconds of the leg only");
        Key back = K(5, 0, 2000.0f, 0.0f, 0, 0.0f, 0.0f);
        Derive(back, tri, 10.0f);
        Require(DirName(back) == "3-1", "3 stops: backwards leg");
        Key other = K(5, 0, 1002.0f, 3.0f, 0, 0.0f, 0.0f);
        Derive(other, tri, 10.0f);
        Require(DirName(other) == "2-1" && !SameEdge(k, other), "same from stop, other to stop: another edge");
    }

    // CLI-PATH stop rule: cyclic neighbours (last + first of the loop) and stops directly in a row merge; a real return stays.
    {
        // 177233 shape: A at node 0, B in the middle, A again as the last node -> 2 stops, AB / BA.
        std::vector<PathNode> const loop = {
            N(1, 0.0f, 0.0f, 30), N(1, 300.0f, 0.0f), N(1, 600.0f, 0.0f, 30), N(1, 300.0f, 100.0f), N(1, 2.0f, 1.0f, 30),
        };
        std::vector<Stop> const ls = StopsOf(loop);
        Require(ls.size() == 2 && ls[0].pos.delay == 60 && ls[0].arriveNode == 4 && ls[0].departNode == 0, "loop end + start: one stop, delays added");
        Key ab = K(177233, 1, 3.0f, 2.0f, 1, 598.0f, 1.0f);
        Derive(ab, loop, 10.0f);
        Require(EdgeName(ab) == "go:177233:AB", "merged loop stop: AB");
        Require(std::fabs(ab.rideS - 60.0f) < 0.01f, "AB from the departure node of the merged stop");
        Key ba = K(177233, 1, 598.0f, 1.0f, 1, 3.0f, 2.0f);
        Derive(ba, loop, 10.0f);
        float const baLen = std::sqrt(300.0f * 300.0f + 100.0f * 100.0f) + std::sqrt(298.0f * 298.0f + 99.0f * 99.0f);
        Require(EdgeName(ba) == "go:177233:BA" && std::fabs(ba.rideS - baLen / 10.0f) < 0.01f, "BA ends at the arrival node of the merged stop");

        std::vector<PathNode> const row = { N(0, 0.0f, 0.0f, 10), N(0, 1.0f, 0.0f, 20), N(0, 500.0f, 0.0f), N(0, 1000.0f, 0.0f, 10) };
        std::vector<Stop> const rs = StopsOf(row);
        Require(rs.size() == 2 && rs[0].pos.delay == 30 && rs[0].arriveNode == 0 && rs[0].departNode == 1, "in a row: one stop");

        std::vector<PathNode> const ret = {
            N(0, 0.0f, 0.0f, 10), N(0, 500.0f, 0.0f), N(0, 1000.0f, 0.0f, 10), N(0, 500.0f, 10.0f), N(0, 0.0f, 1.0f, 10),
            N(0, 1000.0f, 1000.0f), N(0, 2000.0f, 0.0f, 10),
        };
        Require(StopsOf(ret).size() == 4, "real return: separate stops");
        Key amb = K(9, 0, 1.0f, 1.0f, 0, 2000.0f, 0.0f);
        Derive(amb, ret, 10.0f);
        Require(DirName(amb) == "-", "real return at the same place: from point equally near two stops, no guessing");

        std::vector<PathNode> const maps = { N(0, 0.0f, 0.0f, 10), N(0, 900.0f, 0.0f, 10), N(1, 0.0f, 0.0f, 10) };
        Require(StopsOf(maps).size() == 3, "same xy on another map: separate stops");
    }

    // No path, lift, no ride, unclear matches.
    {
        Key k = K(176999, 1, 10.0f, 10.0f, 1, 10.0f, 10.0f);
        Derive(k, {}, 30.0f);
        Require(k.stops == 0 && DirName(k) == "-" && EdgeName(k) == "go:176999:-" && k.rideS == -1.0f, "ship without a path: dir -");
        Key lift = K(47296, 1, 10.0f, 10.0f, 1, 10.0f, 10.0f);
        lift.lift = true;
        Derive(lift, {}, 30.0f);
        Require(DirName(lift) == "lift" && EdgeName(lift) == "go:47296:lift", "elevator / tram: own key, not the ambiguous '-'");
        Key notLift = lift;
        notLift.lift = false;
        Require(!SameEdge(lift, notLift), "lift and unknown ship keys never match");
        Key none = K(0, 1, 10.0f, 10.0f, 1, 20.0f, 20.0f);
        Require(EdgeName(none) == "-", "no ride: edge -");
        Key remote = K(176244, 1, 8000.0f, 2000.0f, 1, 6575.0f, 768.0f);
        Derive(remote, ship, 30.0f);
        Require(DirName(remote) == "-", "from point far from every stop: -");
        std::vector<PathNode> const twin = { N(0, 0.0f, 0.0f, 1), N(0, 50.0f, 0.0f), N(0, 8.0f, 0.0f, 1) };
        Key amb = K(9, 0, 4.0f, 0.0f, 0, 400.0f, 0.0f);
        Derive(amb, twin, 10.0f);
        Require(amb.dirFrom == -1 && DirName(amb) == "-", "about equally far from two stops: - (no guessing)");
        Key otherMap = K(176244, 0, 8546.0f, 1023.0f, 1, 6575.0f, 768.0f);
        Derive(otherMap, ship, 30.0f);
        Require(otherMap.dirFrom == -1, "stops of another map never match");
    }

    // Same edge: ride, direction, from map, from point within SamePointYards.
    {
        Key a = K(1, 0, 0.0f, 0.0f, 0, 100.0f, 0.0f), b = a;
        b.fromX = 3.0f;
        Require(SameEdge(a, b), "same edge within 5 yd");
        b.fromX = 15.0f;
        Require(SameEdge(a, b), "unknown direction: dock node and stop node (15 yd) are one edge");
        b.fromX = 25.0f;
        Require(!SameEdge(a, b), "unknown direction: other dock point");
        b = a;
        b.fromMap = 1;
        Require(!SameEdge(a, b), "unknown direction: other map");
        Key d1 = a, d2 = a;
        d1.dirFrom = d2.dirFrom = 0;
        d1.dirTo = d2.dirTo = 1;
        d2.fromX = 300.0f;
        Require(SameEdge(d1, d2), "known direction: ride + direction decide");
        d2.dirFrom = 1;
        d2.dirTo = 0;
        Require(!SameEdge(d1, d2), "known direction: AB is not BA");
        b = a;
        b.ride = 2;
        Require(!SameEdge(a, b), "other ride");
        Key invalid;
        Require(!SameEdge(invalid, invalid), "invalid keys never match");
    }

    // Exit side (semantics review finding 1): the aboard exit at the departure dock is no arrival.
    {
        Key k = K(176244, 1, 8546.0f, 1023.0f, 1, 6575.0f, 768.0f);
        Require(ExitSide(k, 1, 8548.0f, 1021.0f) == Side::From, "exit next to the from point: from");
        Require(ExitSide(k, 1, 6577.0f, 770.0f) == Side::To, "exit next to the to point: to");
        Require(ExitSide(k, 0, 6577.0f, 770.0f) == Side::Unknown, "exit on neither map: unknown");
        Require(ExitSide(k, 1, 7565.0f, 896.0f) == Side::Unknown, "about halfway (9 yd nearer the from point): unknown (no guessing)");
        Key x = K(164871, 1, 1320.0f, -4650.0f, 0, 2062.0f, 293.0f);
        Require(ExitSide(x, 1, 1300.0f, -4600.0f) == Side::From, "cross-map edge, exit on the from map: from");
        Require(ExitSide(x, 0, 2060.0f, 290.0f) == Side::To, "cross-map edge, exit on the to map: to");
        Key lift = k;
        lift.lift = true;
        Require(ExitSide(lift, 1, 8548.0f, 1021.0f) == Side::Unknown, "lift: from / to differ in height, side unknown");
        Require(std::string(SideName(Side::From)) == "from" && std::string(SideName(Side::Unknown)) == "-", "side names");
    }

    // Run scan and the callers' cache (nochange review findings 1 / 3, semantics nit 10).
    {
        std::vector<RunPoint> path = {
            P(true, 0, 1, 8546.0f, 1023.0f),          // dock hop (unfilled, entry 0) = handed point
            P(true, 176244, 1, 8550.0f, 1020.0f),     // ride leg
            P(true, 176244, 1, 6580.0f, 770.0f),
            P(true, 0, 1, 6575.0f, 768.0f),           // dock hop at the other side = to
            P(false, 0, 1, 6500.0f, 700.0f),          // walk on
        };
        auto at = [&path](std::size_t i) { return path[i]; };
        RunId const id = ScanRun(0, path.size(), at);
        Require(id.ride == 176244 && id.rideOff == 1 && id.runLen == 4 && id.hop == 0, "scan: ride, its offset, run length");
        Require(id.toMap == 1 && id.toX == 6575.0f && id.toY == 768.0f, "scan: to = last point of the run");
        Require(RunStill(id, 0, path.size(), at), "unchanged path: cached run still valid");
        Key const key = KeyOf(id, true);
        Require(key.valid && key.ride == 176244 && key.minimal && key.fromX == 8546.0f && key.toX == 6575.0f, "key of the run");

        std::vector<RunPoint> mid = path;
        mid[2].x += 50.0f;
        auto atMid = [&mid](std::size_t i) { return mid[i]; };
        Require(RunStill(id, 0, mid.size(), atMid), "a point inside the run moved: same key, still valid");

        std::vector<RunPoint> replan = path;   // same handed point and ride, another stop (to) of the vessel
        replan[3] = P(true, 0, 1, 7000.0f, 300.0f);
        auto atReplan = [&replan](std::size_t i) { return replan[i]; };
        Require(!RunStill(id, 0, replan.size(), atReplan), "same from point, other to point: re-derive");

        std::vector<RunPoint> otherRide = path;   // same handed point, another vessel from the same dock
        otherRide[1].entry = 175080;
        otherRide[2].entry = 175080;
        auto atOtherRide = [&otherRide](std::size_t i) { return otherRide[i]; };
        Require(!RunStill(id, 0, otherRide.size(), atOtherRide), "same from point, other ride: re-derive");

        std::vector<RunPoint> longer = path;   // the run goes on past the cached end
        longer[4] = P(true, 0, 1, 6500.0f, 700.0f);
        auto atLonger = [&longer](std::size_t i) { return longer[i]; };
        Require(!RunStill(id, 0, longer.size(), atLonger), "run longer than cached: re-derive");

        std::vector<RunPoint> hopFilled = path;   // the dock hop entry filled (S3b data fix): hop changes
        hopFilled[0].entry = 176244;
        auto atHop = [&hopFilled](std::size_t i) { return hopFilled[i]; };
        Require(!RunStill(id, 0, hopFilled.size(), atHop), "other hop entry: re-derive");

        std::vector<RunPoint> twoShips = {
            P(true, 0, 0, 0.0f, 0.0f), P(true, 11, 0, 10.0f, 0.0f), P(true, 0, 0, 500.0f, 0.0f), P(true, 22, 0, 510.0f, 0.0f),
            P(true, 0, 0, 900.0f, 0.0f),
        };
        auto atTwo = [&twoShips](std::size_t i) { return twoShips[i]; };
        RunId const first = ScanRun(0, twoShips.size(), atTwo);
        Require(first.ride == 11 && first.runLen == 3 && first.toX == 500.0f, "a second vessel ends the run");
        Require(RunStill(first, 0, twoShips.size(), atTwo), "run ended by a second vessel: still valid");

        std::vector<RunPoint> walk = { P(false, 0, 0, 1.0f, 2.0f), P(true, 5, 0, 3.0f, 4.0f) };
        auto atWalk = [&walk](std::size_t i) { return walk[i]; };
        RunId const none = ScanRun(0, walk.size(), atWalk);
        Require(none.runLen == 0 && none.ride == 0 && none.toX == 1.0f && none.rideOff == NoRide, "start is no transport point: empty run");
        Require(RunStill(none, 0, walk.size(), atWalk) && !RunStill(none, 5, walk.size(), atWalk), "empty run cached; out of range never");

        // Two slots: dock node / stop node alternating hits the cache; a third run replaces the less recently used.
        State s;
        Require(!s.Active() && !s.UseCached(0, path.size(), at, false), "fresh state: inactive, nothing cached");
        s.Store(id, key);
        Require(s.Active() && s.current == 0 && s.Current().ride == 176244, "first run in slot 0, current");
        RunId const id2 = ScanRun(0, replan.size(), atReplan);
        s.Store(id2, KeyOf(id2, false));
        Require(s.current == 1 && s.Current().toX == 7000.0f, "second run in slot 1");
        Require(s.UseCached(0, path.size(), at, false) && s.current == 0 && !s.Current().minimal, "first path again: slot 0, minimal updated");
        Require(s.UseCached(0, replan.size(), atReplan, true) && s.current == 1 && s.Current().minimal, "second path again: slot 1");
        Require(s.UseCached(0, path.size(), at, false) && s.current == 0, "alternating: no re-derive");
        RunId const id3 = ScanRun(0, otherRide.size(), atOtherRide);
        s.Store(id3, KeyOf(id3, false));
        Require(s.current == 1 && s.Current().ride == 175080, "third run replaces the less recently used slot (1)");
        Require(s.UseCached(0, path.size(), at, false) && !s.UseCached(0, replan.size(), atReplan, false), "slot 0 kept, slot 1 replaced");
        s.current = State::EpisodeKey;
        s.episode.key = K(42, 0, 0.0f, 0.0f, 0, 1.0f, 1.0f);
        Require(s.Current().ride == 42, "aboard: the episode key is current");
        State copy = s;
        Require(copy.Current().ride == 42, "a copied state keeps a valid current key (index, not pointer)");
    }

    std::uint32_t const timeout = WaitTimeoutMs;
    Key const e1 = K(176244, 1, 8546.0f, 1023.0f, 1, 6575.0f, 768.0f);
    Key const e2 = K(164871, 1, 1300.0f, -4600.0f, 0, 2062.0f, 293.0f);

    // Wait: attempt once, silent ticks, wait_timeout exactly once, then board and arrive.
    {
        State s;
        Step st = OnWait(s, e1, 1000, timeout);
        Require(st.attempt && !st.routeChanged && !st.left && !st.noRide && !st.waitTimeout, "first wait tick: attempt");
        Require(s.episode.phase == Phase::Attempt, "phase attempt");
        SilentTicks(s, e1, 1000, 1000 + timeout - 1, "same key: silent ticks");
        st = OnWait(s, e1, 1000 + timeout - 1, timeout);
        Require(!st.Any(), "just below the threshold: silent");
        st = OnWait(s, e1, 1000 + timeout, timeout);
        Require(st.waitTimeout && !st.attempt && st.waitMs == timeout, "wait_timeout at the threshold");
        SilentTicks(s, e1, 1000 + timeout, 1000 + 3 * timeout, "wait_timeout once per episode");
        st = OnBoardTry(s, e1, 1000 + 3 * timeout - 5000, true, false);
        Require(st.board && !st.attempt && st.waitMs == 3 * timeout - 5000, "board after the timeout: wait_ms of the whole wait");
        Require(s.episode.phase == Phase::Board, "phase board");
        st = OnAboard(s, 1000 + 3 * timeout, timeout);
        Require(!st.Any(), "aboard below the threshold: silent");
        st = OnArrive(s, 1000 + 3 * timeout - 5000 + 60000, Side::To);
        Require(st.arrive && !st.exitOrigin && st.rideMs == 60000 && SameEdge(st.previous, e1), "arrive: ride_ms, key of the episode");
        Require(s.episode.phase == Phase::None, "arrive ends the episode");
        st = OnArrive(s, 1000 + 3 * timeout + 70000, Side::To);
        Require(!st.Any(), "no second arrive");
    }

    // The aboard exit at the departure side: fail exit_origin, not arrive; an unknown side stays an arrive.
    {
        State s;
        OnBoardTry(s, e1, 0, true, false);
        Step st = OnArrive(s, 1000, Side::From);
        Require(st.exitOrigin && !st.arrive && st.rideMs == 1000 && s.episode.phase == Phase::None, "exit at the from side: exit_origin ends the episode");
        OnBoardTry(s, e1, 2000, true, false);
        st = OnArrive(s, 62000, Side::Unknown);
        Require(st.arrive && !st.exitOrigin, "unknown side: arrive (side=- on the line)");
    }

    // MoveTo2 hands the dock node, then the stop node of the same dock (12 yd apart): one episode, no route_changed.
    {
        Key dock = K(176244, 1, 8538.0f, 1023.0f, 1, 6575.0f, 768.0f);
        Key stop = K(176244, 1, 8550.0f, 1020.0f, 1, 6575.0f, 768.0f);
        Derive(dock, ship, 30.0f);
        Derive(stop, ship, 30.0f);
        State s;
        Require(OnWait(s, dock, 0, timeout).attempt, "dock node: attempt");
        Require(!OnWait(s, stop, 1000, timeout).Any(), "stop node of the same dock: same episode");
    }

    // Immediate boarding: attempt + board in one step; overrun once.
    {
        State s;
        Step st = OnBoardTry(s, e1, 500, true, false);
        Require(st.attempt && st.board && st.waitMs == 0, "first contact boards: attempt and board");
        st = OnAboard(s, 500 + timeout, timeout);
        Require(st.overrun && st.aboard && st.rideMs == timeout, "overrun at the threshold");
        st = OnAboard(s, 500 + 2 * timeout, timeout);
        Require(!st.Any(), "overrun once");
        Require(s.episode.phase == Phase::Board, "overrun keeps the ride");
    }

    // board_point: once per episode; a walk approach (not boarded, not failed) is silent.
    {
        State s;
        Step st = OnWait(s, e1, 0, timeout);
        st = OnBoardTry(s, e1, 100, false, true);
        Require(st.boardPoint && !st.attempt && st.waitMs == 100, "board_point fail");
        st = OnBoardTry(s, e1, 200, false, true);
        Require(!st.Any(), "board_point once per episode");
        State w;
        OnWait(w, e1, 0, timeout);
        st = OnBoardTry(w, e1, 100, false, false);
        Require(!st.Any(), "walking to the vessel: no line");
    }

    // route_changed: an attempted key replaced within the gap; the observed wait, not the time since.
    {
        State s;
        OnWait(s, e1, 0, timeout);
        OnWait(s, e1, 4000, timeout);
        Step st = OnWait(s, e2, 100000, timeout);
        Require(st.routeChanged && !st.left && SameEdge(st.previous, e1) && st.previousWaitMs == 4000, "route_changed with the old key");
        Require(st.attempt && SameEdge(s.episode.key, e2), "new attempt for the new key");
    }

    // left (semantics review finding 2): a wait the bot walked away from, and a ride that ended without a hook.
    {
        State s;
        OnWait(s, e1, 0, timeout);
        OnWait(s, e1, 300000, timeout);   // 5 min of waiting
        Step st = OnWait(s, e1, 300000 + GapMs, timeout);
        Require(!st.Any(), "exactly the gap: still the same wait");
        st = OnWait(s, e1, 300000 + 2 * GapMs + 1, timeout);
        Require(st.left && !st.leftAboard && !st.routeChanged && st.previousWaitMs == 300000 + GapMs, "same edge after the gap: left, observed wait");
        Require(st.attempt && !st.waitTimeout && s.episode.waitStartMs == 300000 + 2 * GapMs + 1, "then a new attempt with a fresh wait start");
        st = OnWait(s, e2, 300000 + 4 * GapMs, timeout);
        Require(st.left && !st.routeChanged && SameEdge(st.previous, e1), "other edge after the gap: left, not route_changed");

        State b;
        OnBoardTry(b, e2, 1000, true, false);
        OnAboard(b, 61000, timeout);
        st = OnWait(b, e1, 70000, timeout);
        Require(st.left && st.leftAboard && st.previousRideMs == 60000 && st.previousWaitMs == 0 && SameEdge(st.previous, e2), "ride ended without a hook: left phase=ride, last seen aboard");
        Require(st.attempt && SameEdge(b.episode.key, e1), "new attempt after the left ride");
    }

    // clear() no longer drops the episode (finding 3): a cache reset keeps it, the next key closes it.
    {
        State s;
        OnWait(s, e1, 0, timeout);
        s.cache[0].set = false;
        s.cache[1].set = false;
        Require(s.Active(), "episode alone keeps the state active");
        Step st = OnWait(s, e2, 2000, timeout);
        Require(st.routeChanged && SameEdge(st.previous, e1), "episode survives a cache reset and is closed by the next key");
    }

    // no_ride once, fell_off and stall_abandon end the episode.
    {
        State s;
        Key const noRide = K(0, 1, 1.0f, 1.0f, 1, 50.0f, 50.0f);
        Step st = OnWait(s, noRide, 0, timeout);
        Require(st.attempt && st.noRide, "no ride entry: attempt + no_ride");
        st = OnWait(s, noRide, 10, timeout);
        Require(!st.Any(), "no_ride once");
        st = OnFellOff(s, 20);
        Require(!st.Any(), "fell_off needs a boarded episode");
        st = OnStallAbandon(s, 30);
        Require(st.stallAbandon && !st.aboard && st.waitMs == 10, "stall_abandon while waiting");
        Require(s.episode.phase == Phase::None, "stall_abandon ends the episode");
        Require(!OnStallAbandon(s, 40).Any(), "no episode: no stall line");

        OnBoardTry(s, e1, 1000, true, false);
        st = OnFellOff(s, 3000);
        Require(st.fellOff && st.rideMs == 2000 && SameEdge(st.previous, e1), "fell_off aboard");
        Require(s.episode.phase == Phase::None, "fell_off ends the episode");
        OnBoardTry(s, e1, 5000, true, false);
        st = OnStallAbandon(s, 9000);
        Require(st.stallAbandon && st.aboard && st.rideMs == 4000, "stall_abandon aboard");
    }

    // Timer wrap (WorldTimer ms).
    {
        State s;
        OnWait(s, e1, 0xFFFFFF00u, timeout);
        SilentTicks(s, e1, 0xFFFFFF00u, 0xFFFFFF00u + timeout, "ticks across the timer wrap: silent");
        Step st = OnWait(s, e1, 0xFFFFFF00u + timeout, timeout);
        Require(st.waitTimeout && st.waitMs == timeout, "wait_timeout across the timer wrap");
    }

    // Line formats.
    {
        BotInfo bot;
        bot.guid = 42;
        bot.level = 7;
        bot.team = 'A';
        bot.atMap = 1;
        bot.atX = 8548.0f;
        bot.atY = 1021.0f;
        Key k = e1;
        Derive(k, ship, 30.0f);
        k.hop = 0;
        k.minimal = true;
        Extra x;
        x.cand = 3;
        x.nearD2d = 12.8f;
        std::string const attempt = FormatAttempt(bot, k, x);
        Require(attempt.rfind("[TransportWait] state=attempt bot=42 level=7 team=A edge=go:176244:AB ride=176244 hop=0 map=1 "
            "from_x=8546.0 from_y=1023.0 to_map=1 to_x=6575.0 to_y=768.0 cand=3 near_d2d=12.8 minimal=1 ride_s_formula=", 0) == 0,
            "attempt line");
        Require(!Has(attempt, " cost="), "no cost= field: the value is not the A* link cost");
        x.got = 176244;
        x.tele = true;
        Require(FormatBoard(bot, k, x, 1234) ==
            "[TransportWait] state=board bot=42 edge=go:176244:AB ride=176244 got=176244 map=1 from_x=8546.0 from_y=1023.0 wait_ms=1234 mode=tele",
            "board line");
        x.tele = false;
        Require(Has(FormatBoard(bot, k, x, 0), " mode=walk"), "board walk mode");
        x.ok = true;
        x.dExit = 2.3f;
        x.side = Side::To;
        Require(FormatArrive(bot, k, x, 60000) ==
            "[TransportWait] state=arrive bot=42 edge=go:176244:AB ride=176244 to_map=1 to_x=6575.0 to_y=768.0 ride_ms=60000 ok=1 d_exit=2.3 "
            "side=to at_map=1 at_x=8548.0 at_y=1021.0",
            "arrive line");
        PointRejects rp;
        rp.z = 61;
        rp.on = 39;
        Require(FormatFail(bot, k, "board_point", 5, 0, false, false, &rp) ==
            "[TransportWait] state=fail bot=42 edge=go:176244:AB ride=176244 map=1 from_x=8546.0 from_y=1023.0 reason=board_point "
            "wait_ms=5 ride_ms=0 phase=wait end=0 at_map=1 at_x=8548.0 at_y=1021.0 rp=z:61/on:39/path:0",
            "board_point line with the RandomPointOnTrans rejects");
        Require(FormatFail(bot, k, "exit_origin", 900000, 1000, true, true) ==
            "[TransportWait] state=fail bot=42 edge=go:176244:AB ride=176244 map=1 from_x=8546.0 from_y=1023.0 reason=exit_origin "
            "wait_ms=900000 ride_ms=1000 phase=ride end=1 at_map=1 at_x=8548.0 at_y=1021.0",
            "other fails: phase / end / at, no rp");
        Snapshot sn;
        sn.attempt = 5;
        sn.board = 2;
        sn.arrive = 1;
        sn.fail = 4;
        sn.failEnd = 2;
        sn.dropped = 3;
        sn.waitMsSum = 5000000000ull;
        sn.rideMsSum = 7;
        sn.minute = 29871234ull;
        sn.minutes = 3;
        Require(FormatSummary(sn) ==
            "[TransportWait] summary attempt=5 board=2 arrive=1 fail=4 fail_end=2 dropped=3 wait_ms_sum=5000000000 ride_ms_sum=7 "
            "min=29871234 mins=3", "summary line");

        // Render: transition order, route_changed with the previous key.
        State s;
        OnWait(s, e2, 0, timeout);
        Key nk = K(0, 1, 1.0f, 1.0f, 1, 9.0f, 9.0f);
        Step st = OnBoardTry(s, nk, 50, true, false);
        std::string lines[MaxLinesPerStep];
        std::size_t n = Render(st, bot, s.episode.key, x, lines);
        Require(n == 4, "route_changed + attempt + no_ride + board");
        Require(Has(lines[0], "state=fail") && Has(lines[0], "edge=go:164871:-") && Has(lines[0], "reason=route_changed") &&
            Has(lines[0], "phase=wait end=1"), "line 1 route_changed old key, ends the episode");
        Require(Has(lines[1], "state=attempt") && Has(lines[1], "edge=- ride=0"), "line 2 attempt");
        Require(Has(lines[2], "reason=no_ride") && Has(lines[2], "phase=wait end=0"), "line 3 no_ride, open");
        Require(Has(lines[3], "state=board"), "line 4 board");
        for (std::size_t i = 0; i < n; ++i)
            Require(lines[i].rfind("[TransportWait] state=", 0) == 0, "state= first: never the old '[TransportWait] bot=' form");

        // left aboard + attempt + no_ride + board: still 4 lines.
        State b;
        OnBoardTry(b, e2, 0, true, false);
        OnAboard(b, 1000, timeout);
        st = OnBoardTry(b, nk, 2000, true, false);
        n = Render(st, bot, b.episode.key, x, lines);
        Require(n == 4 && Has(lines[0], "reason=left") && Has(lines[0], "phase=ride end=1") && Has(lines[0], "ride_ms=1000"), "left aboard line first");
        // exit_origin renders the previous key, phase=ride end=1.
        State o;
        OnBoardTry(o, e1, 0, true, false);
        st = OnArrive(o, 1000, Side::From);
        n = Render(st, bot, o.episode.key, x, lines);
        Require(n == 1 && Has(lines[0], "reason=exit_origin") && Has(lines[0], "edge=go:176244:") && Has(lines[0], "phase=ride end=1"), "exit_origin line");
        Step ov;
        ov.overrun = true;
        n = Render(ov, bot, e1, x, lines);
        Require(n == 1 && Has(lines[0], "reason=overrun") && Has(lines[0], "phase=ride end=0"), "overrun open");
        Step sa;
        sa.stallAbandon = true;
        sa.aboard = true;
        sa.previous = e1;
        n = Render(sa, bot, e1, x, lines);
        Require(n == 1 && Has(lines[0], "reason=stall_abandon") && Has(lines[0], "phase=ride end=1"), "stall_abandon aboard ends");
    }

    // Counters: accounting (the documented sums), cap, dropped, roll.
    {
        Counters c;
        Snapshot sn;
        Require(!c.Roll(600, sn), "no summary for the very first minute");
        for (std::uint32_t i = 0; i < LinesPerMinute; ++i)
            Require(c.Take(), "within the cap");
        Require(!c.Take() && !c.Take(), "over the cap");
        Step a;
        a.attempt = true;
        a.noRide = true;
        c.Account(a);                      // fail 1 (open)
        Step b;
        b.board = true;
        b.waitMs = 700;
        c.Account(b);                      // wait 700
        Step r;
        r.arrive = true;
        r.rideMs = 300;
        r.waitMs = 700;                    // already summed at board
        c.Account(r);                      // ride 300
        Step rc;
        rc.routeChanged = true;
        rc.previousWaitMs = 50;
        rc.attempt = true;
        c.Account(rc);                     // fail 1 (end), wait 50
        Step sa;
        sa.stallAbandon = true;
        sa.aboard = true;
        sa.waitMs = 999;
        sa.rideMs = 11;
        c.Account(sa);                     // fail 1 (end), ride 11
        Step lw;
        lw.left = true;
        lw.previousWaitMs = 1000;
        lw.attempt = true;
        c.Account(lw);                     // fail 1 (end), wait 1000
        Step lr;
        lr.left = true;
        lr.leftAboard = true;
        lr.previousWaitMs = 5;
        lr.previousRideMs = 2000;
        c.Account(lr);                     // fail 1 (end), ride 2000
        Step eo;
        eo.exitOrigin = true;
        eo.waitMs = 77;
        eo.rideMs = 3;
        c.Account(eo);                     // fail 1 (end), ride 3
        Step ov;
        ov.overrun = true;
        ov.waitMs = 4;
        ov.rideMs = 900000;
        c.Account(ov);                     // fail 1 (open), nothing summed
        Require(!c.Roll(659, sn), "same minute: no summary");
        Require(c.Roll(660, sn), "next minute: summary");
        Require(sn.attempt == 3 && sn.board == 1 && sn.arrive == 1 && sn.fail == 7 && sn.failEnd == 5 && sn.dropped == 2,
            "counts, ending fails and dropped lines");
        Require(sn.waitMsSum == 1750 && sn.rideMsSum == 2314, "sums: board + phase=wait end=1, arrive + phase=ride end=1");
        Require(sn.minute == 10 && sn.minutes == 1, "window: minute 10, one minute");
        Require(c.Take(), "cap starts again in the new minute");
        Require(c.Roll(900, sn) && sn.attempt == 0 && sn.dropped == 0 && sn.minute == 11 && sn.minutes == 4, "idle minutes: one window of 4 minutes");

        // compile review finding 1: an over-budget Take never gives the increment back, so a Roll in between cannot wrap it.
        Counters w;
        w.Roll(60, sn);
        for (std::uint32_t i = 0; i < LinesPerMinute + 5; ++i)
            w.Take();
        w.Roll(120, sn);
        Require(sn.dropped == 5, "5 dropped in the full minute");
        std::uint32_t taken = 0;
        for (std::uint32_t i = 0; i < LinesPerMinute + 1; ++i)
            taken += w.Take() ? 1u : 0u;
        Require(taken == LinesPerMinute && w.logged.load() == LinesPerMinute + 1, "after the roll: the full budget again, no give-back");
    }

    // 8 map threads: the cap holds, nothing is lost, one summary per minute.
    {
        Counters c;
        Snapshot sn;
        c.Roll(60, sn);
        std::atomic<std::uint32_t> taken{0};
        std::atomic<std::uint32_t> summaries{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&c, &taken, &summaries]() {
                Snapshot local;
                for (int i = 0; i < 1000; ++i)
                {
                    if (c.Roll(70, local))
                        summaries.fetch_add(1);
                    Step st;
                    st.attempt = true;
                    st.board = true;
                    st.waitMs = 2;
                    c.Account(st);
                    if (c.Take())
                        taken.fetch_add(1);
                }
            });
        for (std::thread& thread : threads)
            thread.join();
        Require(taken.load() == LinesPerMinute, "exactly the cap is logged under concurrency");
        Require(summaries.load() == 0, "same minute: no summary from any thread");
        Require(c.Roll(120, sn), "one summary in the next minute");
        Require(sn.attempt == 8000 && sn.board == 8000 && sn.waitMsSum == 16000, "every transition counted");
        Require(sn.dropped == 8000 - LinesPerMinute, "dropped = lines over the cap");
        std::atomic<std::uint32_t> rolled{0};
        std::vector<std::thread> rollers;
        for (int t = 0; t < 8; ++t)
            rollers.emplace_back([&c, &rolled]() {
                Snapshot local;
                if (c.Roll(180, local))
                    rolled.fetch_add(1);
            });
        for (std::thread& thread : rollers)
            thread.join();
        Require(rolled.load() == 1, "exactly one thread gets the minute's summary");
    }

    std::cout << "transport_edge_policy_tests passed\n";
    return 0;
}
