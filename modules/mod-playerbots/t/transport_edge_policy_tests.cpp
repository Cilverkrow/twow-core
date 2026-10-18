#include "TransportEdgePolicy.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// twow-repo#342 (S1, AiPlayerbot.Path.TransportEdgeTrace): edge key, episode state machine, line formats, counters.
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

bool Has(std::string const& s, char const* part) { return s.find(part) != std::string::npos; }
}

int main()
{
    // Threshold: default 15, clamped 1..240, in ms.
    Require(ClampWaitTimeoutMin(15) == 15 && DefaultWaitTimeoutMin == 15, "default 15 min");
    Require(ClampWaitTimeoutMin(0) == 1 && ClampWaitTimeoutMin(-5) == 1, "clamped up to 1");
    Require(ClampWaitTimeoutMin(1000) == 240, "clamped down to 240");
    Require(TimeoutMs(15) == 900000u, "15 min = 900000 ms (map: wait_timeout at 900 s)");

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
        Require(ab.cost > 0.0f, "ride cost from the TaxiPath length / moveSpeed");
        float const len = std::sqrt(550.0f * 550.0f + 120.0f * 120.0f) + std::sqrt(1000.0f * 1000.0f + 400.0f * 400.0f) +
            std::sqrt(420.0f * 420.0f + 270.0f * 270.0f);
        Require(std::fabs(ab.cost - len / 30.0f) < 0.01f, "cost = path length A..B / speed");

        Key ba = K(176244, 1, 6575.0f, 768.0f, 1, 8546.0f, 1023.0f);
        Derive(ba, ship, 30.0f);
        Require(DirName(ba) == "BA" && EdgeName(ba) == "go:176244:BA", "2 stops: BA");
        Require(ba.cost > 0.0f && std::fabs(ba.cost - ab.cost) > 0.01f, "BA cost wraps around the path end");

        Key noSpeed = ab;
        Derive(noSpeed, ship, 0.0f);
        Require(noSpeed.cost == -1.0f && DirName(noSpeed) == "AB", "no move speed: cost unknown, direction kept");
    }

    // Three stops: <i>-<j>, 0-based in TaxiPath order.
    {
        std::vector<PathNode> const tri = {
            N(0, 0.0f, 0.0f, 1000), N(0, 500.0f, 0.0f), N(0, 1000.0f, 0.0f, 1000), N(0, 1500.0f, 0.0f), N(0, 2000.0f, 0.0f, 1000),
        };
        Key k = K(5, 0, 1002.0f, 3.0f, 0, 1998.0f, -2.0f);
        Derive(k, tri, 10.0f);
        Require(k.stops == 3 && DirName(k) == "2-3" && EdgeName(k) == "go:5:2-3", "3 stops: i-j, ordinals from 1");
        Require(std::fabs(k.cost - 100.0f) < 0.01f, "3 stops: cost of the leg only");
        Key back = K(5, 0, 2000.0f, 0.0f, 0, 0.0f, 0.0f);
        Derive(back, tri, 10.0f);
        Require(DirName(back) == "3-1", "3 stops: backwards leg");
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
        Require(std::fabs(ab.cost - 60.0f) < 0.01f, "AB cost from the departure node of the merged stop");
        Key ba = K(177233, 1, 598.0f, 1.0f, 1, 3.0f, 2.0f);
        Derive(ba, loop, 10.0f);
        float const baLen = std::sqrt(300.0f * 300.0f + 100.0f * 100.0f) + std::sqrt(298.0f * 298.0f + 99.0f * 99.0f);
        Require(EdgeName(ba) == "go:177233:BA" && std::fabs(ba.cost - baLen / 10.0f) < 0.01f, "BA ends at the arrival node of the merged stop");

        // Two delay>0 nodes directly in a row at one place: one stop.
        std::vector<PathNode> const row = { N(0, 0.0f, 0.0f, 10), N(0, 1.0f, 0.0f, 20), N(0, 500.0f, 0.0f), N(0, 1000.0f, 0.0f, 10) };
        std::vector<Stop> const rs = StopsOf(row);
        Require(rs.size() == 2 && rs[0].pos.delay == 30 && rs[0].arriveNode == 0 && rs[0].departNode == 1, "in a row: one stop");

        // A real return: the same place again far apart in point order stays its own stop.
        std::vector<PathNode> const ret = {
            N(0, 0.0f, 0.0f, 10), N(0, 500.0f, 0.0f), N(0, 1000.0f, 0.0f, 10), N(0, 500.0f, 10.0f), N(0, 0.0f, 1.0f, 10),
            N(0, 1000.0f, 1000.0f), N(0, 2000.0f, 0.0f, 10),
        };
        Require(StopsOf(ret).size() == 4, "real return: separate stops");
        Key amb = K(9, 0, 1.0f, 1.0f, 0, 2000.0f, 0.0f);
        Derive(amb, ret, 10.0f);
        Require(DirName(amb) == "-", "real return at the same place: from point equally near two stops, no guessing");

        // Same place on another map does not merge.
        std::vector<PathNode> const maps = { N(0, 0.0f, 0.0f, 10), N(0, 900.0f, 0.0f, 10), N(1, 0.0f, 0.0f, 10) };
        Require(StopsOf(maps).size() == 3, "same xy on another map: separate stops");
    }

    // No path (elevator / tram / unknown entry), no ride, unclear matches.
    {
        Key k = K(47296, 1, 10.0f, 10.0f, 1, 10.0f, 10.0f);
        Derive(k, {}, 30.0f);
        Require(k.stops == 0 && DirName(k) == "-" && EdgeName(k) == "go:47296:-" && k.cost == -1.0f, "no path: dir -");
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

    // Same edge: ride, direction, from map, from point within 5 yd.
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
        b.dirFrom = 1;
        Require(!SameEdge(a, b), "other direction");
        b = a;
        b.ride = 2;
        Require(!SameEdge(a, b), "other ride");
        Key invalid;
        Require(!SameEdge(invalid, invalid), "invalid keys never match");
    }

    std::uint32_t const timeout = TimeoutMs(15);
    Key const e1 = K(176244, 1, 8546.0f, 1023.0f, 1, 6575.0f, 768.0f);
    Key const e2 = K(164871, 1, 1300.0f, -4600.0f, 0, 2062.0f, 293.0f);

    // Wait: attempt once, silent ticks, wait_timeout exactly once, then board and arrive.
    {
        State s;
        Step st = OnWait(s, e1, 1000, timeout);
        Require(st.attempt && !st.routeChanged && !st.noRide && !st.waitTimeout, "first wait tick: attempt");
        Require(s.episode.phase == Phase::Attempt, "phase attempt");
        st = OnWait(s, e1, 2000, timeout);
        Require(!st.Any(), "same key: silent tick");
        st = OnWait(s, e1, 1000 + timeout - 1, timeout);
        Require(!st.Any(), "just below the threshold: silent");
        st = OnWait(s, e1, 1000 + timeout, timeout);
        Require(st.waitTimeout && !st.attempt && st.waitMs == timeout, "wait_timeout at the threshold");
        st = OnWait(s, e1, 1000 + 3 * timeout, timeout);
        Require(!st.Any(), "wait_timeout once per episode");
        st = OnBoardTry(s, e1, 1000 + 3 * timeout + 5, true, false);
        Require(st.board && !st.attempt && st.waitMs == 3 * timeout + 5, "board after the timeout: wait_ms of the whole wait");
        Require(s.episode.phase == Phase::Board, "phase board");
        st = OnAboard(s, 1000 + 3 * timeout + 100, timeout);
        Require(!st.Any(), "aboard below the threshold: silent");
        st = OnArrive(s, 1000 + 3 * timeout + 5 + 60000);
        Require(st.arrive && st.rideMs == 60000 && SameEdge(st.previous, e1), "arrive: ride_ms, key of the episode");
        Require(s.episode.phase == Phase::None, "arrive ends the episode");
        st = OnArrive(s, 1000 + 3 * timeout + 5 + 70000);
        Require(!st.Any(), "no second arrive");
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
        Require(st.overrun && st.rideMs == timeout, "overrun at the threshold");
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

    // route_changed: an attempted key replaced; the observed wait, not the time since.
    {
        State s;
        OnWait(s, e1, 0, timeout);
        OnWait(s, e1, 4000, timeout);
        Step st = OnWait(s, e2, 100000, timeout);
        Require(st.routeChanged && SameEdge(st.previous, e1) && st.previousWaitMs == 4000, "route_changed with the old key");
        Require(st.attempt && SameEdge(s.episode.key, e2), "new attempt for the new key");
        // A boarded episode the bot is no longer on: a new attempt, no route_changed.
        OnBoardTry(s, e2, 100100, true, false);
        st = OnWait(s, e1, 200000, timeout);
        Require(st.attempt && !st.routeChanged, "after an unseen ride end: new attempt only");
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
        Step st = OnWait(s, e1, 0xFFFFFF00u + timeout, timeout);
        Require(st.waitTimeout && st.waitMs == timeout, "wait_timeout across the timer wrap");
    }

    // Source cache of the callers.
    {
        State s;
        Require(!s.Active() && !s.SourceIs(1, 1.0f, 2.0f, 0), "fresh state: inactive, no source");
        s.SetSource(1, 1.0f, 2.0f, 0, e1);
        Require(s.Active() && s.SourceIs(1, 1.0f, 2.0f, 0), "source set");
        Require(!s.SourceIs(1, 1.0f, 2.5f, 0) && !s.SourceIs(1, 1.0f, 2.0f, 7) && !s.SourceIs(0, 1.0f, 2.0f, 0), "other point / entry / map");
    }

    // Line formats.
    {
        BotInfo bot;
        bot.guid = 42;
        bot.level = 7;
        bot.team = 'A';
        Key k = e1;
        Derive(k, ship, 30.0f);
        k.hop = 0;
        k.minimal = true;
        Extra x;
        x.cand = 3;
        x.nearD2d = 12.8f;
        std::string const attempt = FormatAttempt(bot, k, x);
        Require(attempt.rfind("[TransportWait] state=attempt bot=42 level=7 team=A edge=go:176244:AB ride=176244 hop=0 map=1 "
            "from_x=8546.0 from_y=1023.0 to_map=1 to_x=6575.0 to_y=768.0 cand=3 near_d2d=12.8 minimal=1 cost=", 0) == 0,
            "attempt line");
        x.got = 176244;
        x.tele = true;
        Require(FormatBoard(bot, k, x, 1234) ==
            "[TransportWait] state=board bot=42 edge=go:176244:AB ride=176244 got=176244 map=1 from_x=8546.0 from_y=1023.0 wait_ms=1234 mode=tele",
            "board line");
        x.tele = false;
        Require(Has(FormatBoard(bot, k, x, 0), " mode=walk"), "board walk mode");
        x.ok = true;
        x.dExit = 2.3f;
        Require(FormatArrive(bot, k, x, 60000) ==
            "[TransportWait] state=arrive bot=42 edge=go:176244:AB ride=176244 to_map=1 to_x=6575.0 to_y=768.0 ride_ms=60000 ok=1 d_exit=2.3",
            "arrive line");
        PointRejects rp;
        rp.z = 61;
        rp.on = 39;
        Require(FormatFail(bot, k, "board_point", 5, 0, &rp) ==
            "[TransportWait] state=fail bot=42 edge=go:176244:AB ride=176244 map=1 from_x=8546.0 from_y=1023.0 reason=board_point "
            "wait_ms=5 ride_ms=0 rp=z:61/on:39/path:0",
            "board_point line with the RandomPointOnTrans rejects");
        Require(FormatFail(bot, k, "wait_timeout", 900000, 0) ==
            "[TransportWait] state=fail bot=42 edge=go:176244:AB ride=176244 map=1 from_x=8546.0 from_y=1023.0 reason=wait_timeout "
            "wait_ms=900000 ride_ms=0",
            "other fails: no rp");
        Snapshot sn;
        sn.attempt = 5;
        sn.board = 2;
        sn.arrive = 1;
        sn.fail = 4;
        sn.dropped = 3;
        sn.waitMsSum = 5000000000ull;
        sn.rideMsSum = 7;
        Require(FormatSummary(sn) ==
            "[TransportWait] summary attempt=5 board=2 arrive=1 fail=4 dropped=3 wait_ms_sum=5000000000 ride_ms_sum=7", "summary line");

        // Render: transition order, route_changed with the previous key.
        State s;
        OnWait(s, e2, 0, timeout);
        Key nk = K(0, 1, 1.0f, 1.0f, 1, 9.0f, 9.0f);
        Step st = OnBoardTry(s, nk, 50, true, false);
        std::string lines[MaxLinesPerStep];
        std::size_t const n = Render(st, bot, s.episode.key, x, lines);
        Require(n == 4, "route_changed + attempt + no_ride + board");
        Require(Has(lines[0], "state=fail") && Has(lines[0], "edge=go:164871:-") && Has(lines[0], "reason=route_changed"), "line 1 route_changed old key");
        Require(Has(lines[1], "state=attempt") && Has(lines[1], "edge=- ride=0"), "line 2 attempt");
        Require(Has(lines[2], "reason=no_ride"), "line 3 no_ride");
        Require(Has(lines[3], "state=board"), "line 4 board");
        // No line starts like the old tick line.
        for (std::size_t i = 0; i < n; ++i)
            Require(lines[i].rfind("[TransportWait] state=", 0) == 0, "state= first: never the old '[TransportWait] bot=' form");
    }

    // Counters: accounting, cap, dropped, roll.
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
        c.Account(a);
        Step b;
        b.board = true;
        b.waitMs = 700;
        c.Account(b);
        Step r;
        r.arrive = true;
        r.rideMs = 300;
        r.waitMs = 700;   // already summed at board
        c.Account(r);
        Step rc;
        rc.routeChanged = true;
        rc.previousWaitMs = 50;
        rc.attempt = true;
        c.Account(rc);
        Step sa;
        sa.stallAbandon = true;
        sa.aboard = true;
        sa.waitMs = 999;
        sa.rideMs = 11;
        c.Account(sa);
        Require(!c.Roll(659, sn), "same minute: no summary");
        Require(c.Roll(660, sn), "next minute: summary");
        Require(sn.attempt == 2 && sn.board == 1 && sn.arrive == 1 && sn.fail == 3 && sn.dropped == 2, "counts and dropped lines");
        Require(sn.waitMsSum == 750 && sn.rideMsSum == 311, "wait / ride sums at the end of each phase");
        Require(c.Take(), "cap starts again in the new minute");
        Require(c.Roll(720, sn) && sn.attempt == 0 && sn.dropped == 0, "counts reset by the roll");
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
