#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ai::transport_edge
{
// twow-repo#342 (S1, AiPlayerbot.Path.TransportEdgeTrace, default 0): one [TransportWait] state= line per transition of a
// bot's transport edge episode (attempt -> board -> arrive, or a fail), never per tick, plus a summary with the unsampled
// counts. Measurement only: nothing here changes where a bot goes or when it boards. Pure: no game includes; the game side
// (MovementActions.cpp) fills Key / Extra and logs what Render returns.
//
// Fail lines carry phase=wait|ride (before / after boarding) and end=0|1 (1 = the line ends the episode). Ending fails:
// route_changed, left, exit_origin, fell_off, stall_abandon. Open fails (board / arrive may still follow): no_ride,
// wait_timeout, board_point, overrun. So per window: attempt = board + ending fails with phase=wait + still open, and
// board = arrive + ending fails with phase=ride + still open.
// Summary sums: wait_ms_sum = wait_ms of the board lines + wait_ms of the fails with phase=wait end=1;
// ride_ms_sum = ride_ms of the arrive lines + ride_ms of the fails with phase=ride end=1. The other wait_ms / ride_ms values
// are informational (the time so far) and not in the sums.

// Own line budget, separate from the RouteFailDetail.LinesPerMinute budget the [TaxiUse] / old [TransportWait] lines share.
constexpr std::uint32_t LinesPerMinute = 120;

// Default of the trace's wait_timeout / overrun threshold (map section 3: 900 s; the longest vessel period is 356,284 ms, so
// about 2.5 round trips). The callers pass AiPlayerbot.Path.TransportWaitTimeoutMin * 60000 (S3b, default 15 = this value).
constexpr std::uint32_t WaitTimeoutMs = 900000;

// Episode gap (the S0 parser's 300 s): a wait or a ride the bot has not been seen in for longer than this, without any hook,
// is closed as fail reason=left when the bot comes back to a transport (well above passiveDelay and the MoveTo2 tick).
constexpr std::uint32_t GapMs = 300000;

// Caller bounds: the NODE_TRANSPORT run scanned for the ride entry (dock hop + ride leg + dock hop) and the TaxiPath nodes.
constexpr std::size_t MaxRunScan = 256;
constexpr std::size_t MaxTaxiNodes = 1024;
// A from / to point names a stop only within this distance (dock nodes are 0-16 yd from their stop) and only when the
// second-nearest stop is at least AmbiguousMarginYards farther away (CLI-PATH: no guessing, '-' instead). The same margin
// decides the exit side of an arrive (ExitSide).
constexpr float MaxStopMatchYards = 60.0f;
constexpr float AmbiguousMarginYards = 10.0f;
// Two keys name the same edge when ride and a known direction match; without a known direction, when ride and from map
// match and the from points are this close (2D). Covers the dock node / stop node pair of one dock (11.8-16 yd apart), which
// MoveTo2 hands over in turn while the bot walks onto the dock.
constexpr float SamePointYards = 20.0f;

// One node of a vessel's TaxiPath (DBC TaxiPathNode); delay > 0 marks a stop.
struct PathNode
{
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t delay = 0;
};

// The edge of one transport run: computed by the caller once per change of the run (see RunId).
struct Key
{
    bool valid = false;
    std::uint32_t ride = 0;     // first non-zero entry in the NODE_TRANSPORT run (the ride leg's GO entry), 0 = none
    std::uint32_t hop = 0;      // entry of the point handed to UseTransport (0 for an unfilled dock hop)
    std::int32_t dirFrom = -1;  // stop index (0-based, merged stops in TaxiPath order) of the from / to point, -1 = unknown
    std::int32_t dirTo = -1;
    std::uint32_t stops = 0;    // stops of the TaxiPath after merging, 0 = no TaxiPath (elevator, tram, unknown entry)
    bool lift = false;          // the ride is a GAMEOBJECT_TYPE_TRANSPORT (elevator / tram car): edge go:<entry>:lift
    std::uint32_t fromMap = 0;  // the handed point (where the bot boards)
    float fromX = 0.0f;
    float fromY = 0.0f;
    std::uint32_t toMap = 0;    // last point of the run (where the bot gets off)
    float toX = 0.0f;
    float toY = 0.0f;
    float rideS = -1.0f;        // ride_s_formula: TaxiPath length / moveSpeed of the leg (see RideSeconds), -1 = unknown
    bool minimal = false;       // seen from MinimalMove (background bot)
};

// A stop of the vessel after the CLI-PATH stop rule (18.10.2026): two delay>0 nodes directly in a row (adjacent in point
// order) or the last and the first stop of the loop, on the same map within SameStopYards, are one stop (delays added,
// 177233: 30 + 30 = 60 s). Same-place stops far apart in point order (a real return) stay separate. arriveNode /
// departNode are the TaxiPath node indices where the vessel arrives at / leaves the stop.
constexpr float SameStopYards = 5.0f;

struct Stop
{
    PathNode pos;
    std::size_t arriveNode = 0;
    std::size_t departNode = 0;
};

inline bool SamePlace(PathNode const& a, PathNode const& b)
{
    float const dx = a.x - b.x, dy = a.y - b.y;
    return a.map == b.map && dx * dx + dy * dy <= SameStopYards * SameStopYards;
}

// The stops of a TaxiPath in point order, A = the first delay>0 stop.
inline std::vector<Stop> StopsOf(std::vector<PathNode> const& nodes)
{
    std::vector<Stop> stops;
    std::size_t const n = nodes.size() < MaxTaxiNodes ? nodes.size() : MaxTaxiNodes;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!nodes[i].delay)
            continue;
        if (!stops.empty() && stops.back().departNode + 1 == i && SamePlace(stops.back().pos, nodes[i]))
        {
            stops.back().departNode = i;   // directly in a row: one stop
            stops.back().pos.delay += nodes[i].delay;
            continue;
        }
        Stop stop;
        stop.pos = nodes[i];
        stop.arriveNode = i;
        stop.departNode = i;
        stops.push_back(stop);
    }
    if (stops.size() > 1 && SamePlace(stops.back().pos, stops.front().pos))
    {
        stops.front().arriveNode = stops.back().arriveNode;   // last and first of the loop: one stop
        stops.front().pos.delay += stops.back().pos.delay;
        stops.pop_back();
    }
    return stops;
}

// Index of the stop nearest to (map, x, y); -1 when no stop of that map is within MaxStopMatchYards or the second-nearest
// is not clearly farther (AmbiguousMarginYards).
inline std::int32_t NearestStop(std::vector<Stop> const& stops, std::uint32_t map, float x, float y)
{
    std::int32_t best = -1;
    float bestD = 0.0f, secondD = -1.0f;
    for (std::size_t i = 0; i < stops.size(); ++i)
    {
        if (stops[i].pos.map != map)
            continue;
        float const dx = stops[i].pos.x - x, dy = stops[i].pos.y - y;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (best < 0 || d < bestD)
        {
            if (best >= 0)
                secondD = bestD;
            best = std::int32_t(i);
            bestD = d;
        }
        else if (secondD < 0.0f || d < secondD)
            secondD = d;
    }
    if (best < 0 || bestD > MaxStopMatchYards)
        return -1;
    if (secondD >= 0.0f && secondD - bestD < AmbiguousMarginYards)
        return -1;
    return best;
}

// ride_s_formula: the TaxiPath length of the leg stop i (departure node) -> stop j (arrival node) in path order, wrapping,
// same-map segments only, divided by the template's moveSpeed. Not the A* link cost: the trace cannot read the graph link
// without a node lookup, and the shipped graph stores another value (0.003333 on 14 legs, i342 map V3); the parser joins
// the stored cost from the SQL. Stop delays and cross-map segments are not in it. -1 = unknown.
inline float RideSeconds(std::vector<PathNode> const& nodes, std::vector<Stop> const& stops, std::int32_t from, std::int32_t to,
    float moveSpeed)
{
    if (from < 0 || to < 0 || from == to || moveSpeed <= 0.0f || std::size_t(from) >= stops.size() ||
        std::size_t(to) >= stops.size() || nodes.empty())
        return -1.0f;
    std::size_t const n = nodes.size() < MaxTaxiNodes ? nodes.size() : MaxTaxiNodes;
    std::size_t i = stops[std::size_t(from)].departNode;
    std::size_t const end = stops[std::size_t(to)].arriveNode;
    float length = 0.0f;
    for (std::size_t steps = 0; i != end && steps < n; ++steps)
    {
        std::size_t const next = (i + 1) % n;
        if (nodes[i].map == nodes[next].map)
        {
            float const dx = nodes[next].x - nodes[i].x, dy = nodes[next].y - nodes[i].y, dz = nodes[next].z - nodes[i].z;
            length += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        i = next;
    }
    return length / moveSpeed;
}

// Direction and ride seconds of a key from the vessel's TaxiPath nodes (empty = no TaxiPath).
inline void Derive(Key& key, std::vector<PathNode> const& nodes, float moveSpeed)
{
    std::vector<Stop> const stops = StopsOf(nodes);
    key.stops = std::uint32_t(stops.size());
    key.dirFrom = NearestStop(stops, key.fromMap, key.fromX, key.fromY);
    key.dirTo = NearestStop(stops, key.toMap, key.toX, key.toY);
    key.rideS = RideSeconds(nodes, stops, key.dirFrom, key.dirTo, moveSpeed);
}

// "lift" for an elevator / tram car (CLI-PATH: own key, not AB/BA), "AB" / "BA" for a two-stop path (A = first delay>0
// stop), "<i>-<j>" for more stops (stop ordinals from 1 after merging, CLI-PATH stop rule), "-" without a path or a clear
// match.
inline std::string DirName(Key const& key)
{
    if (key.lift)
        return "lift";
    if (key.stops < 2 || key.dirFrom < 0 || key.dirTo < 0 || key.dirFrom == key.dirTo)
        return "-";
    if (key.stops == 2)
        return key.dirFrom == 0 ? "AB" : "BA";
    return std::to_string(key.dirFrom + 1) + "-" + std::to_string(key.dirTo + 1);
}

// go:<entry>:<dir> (the P3-d rev3 / #603 id), "-" without a ride entry.
inline std::string EdgeName(Key const& key)
{
    if (!key.ride)
        return "-";
    return "go:" + std::to_string(key.ride) + ":" + DirName(key);
}

inline bool SameEdge(Key const& a, Key const& b)
{
    if (!a.valid || !b.valid || a.ride != b.ride || a.dirFrom != b.dirFrom || a.dirTo != b.dirTo || a.lift != b.lift)
        return false;
    if (a.ride && a.dirFrom >= 0 && a.dirTo >= 0)
        return true;
    if (a.fromMap != b.fromMap)
        return false;
    float const dx = a.fromX - b.fromX, dy = a.fromY - b.fromY;
    return dx * dx + dy * dy <= SamePointYards * SamePointYards;
}

// Where the bot was when it got off (its position right before MoveOffTransport, i.e. on the vessel) relative to the key:
// From = on the from map and clearly nearer the from point (or the to point is on another map), To likewise, Unknown
// otherwise (neither map, about equally near, or a lift whose from / to differ mostly in height). From is not an arrival:
// the aboard exit fired at the departure dock (fail reason=exit_origin).
enum class Side : std::uint8_t { Unknown, From, To };

inline Side ExitSide(Key const& key, std::uint32_t map, float x, float y)
{
    if (!key.valid || key.lift)
        return Side::Unknown;
    bool const onFrom = key.fromMap == map, onTo = key.toMap == map;
    float const fx = key.fromX - x, fy = key.fromY - y, tx = key.toX - x, ty = key.toY - y;
    float const dFrom = std::sqrt(fx * fx + fy * fy), dTo = std::sqrt(tx * tx + ty * ty);
    if (onFrom && (!onTo || dFrom + AmbiguousMarginYards <= dTo))
        return Side::From;
    if (onTo && (!onFrom || dTo + AmbiguousMarginYards <= dFrom))
        return Side::To;
    return Side::Unknown;
}

inline char const* SideName(Side side) { return side == Side::From ? "from" : side == Side::To ? "to" : "-"; }

// RandomPointOnTrans rejects (optional out-param, default nullptr there): z window, isOnTransport, walk path.
struct PointRejects
{
    std::uint32_t z = 0;
    std::uint32_t on = 0;
    std::uint32_t path = 0;
};

// One point of the caller's path as the run scan sees it.
struct RunPoint
{
    bool transport = false;     // PathNodeType::NODE_TRANSPORT
    std::uint32_t entry = 0;
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
};

constexpr std::size_t NoRide = static_cast<std::size_t>(-1);

// Everything a key depends on: the handed point (from, hop), the ride entry and where it sits in the run, and the last
// point of the run (to). The callers' cache is keyed on this, so a re-planned path with the same handed point but another
// ride or another to point derives a new key.
struct RunId
{
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t hop = 0;
    std::uint32_t ride = 0;
    std::size_t rideOff = NoRide;   // offset of the first non-zero entry from the start of the run
    std::size_t runLen = 0;         // points in the run (0 = the start point is no NODE_TRANSPORT)
    std::uint32_t toMap = 0;
    float toX = 0.0f;
    float toY = 0.0f;
};

// True when the scan stops at `p`: not a transport point, or a second vessel (another non-zero entry after the ride).
inline bool EndsRun(RunPoint const& p, std::uint32_t ride) { return !p.transport || (ride && p.entry && p.entry != ride); }

// The run that starts at start (start < size), at most MaxRunScan points. at(i) returns the RunPoint of path point i.
template <class At>
RunId ScanRun(std::size_t start, std::size_t size, At const& at)
{
    RunId id;
    RunPoint const first = at(start);
    id.map = first.map;
    id.x = first.x;
    id.y = first.y;
    id.hop = first.entry;
    std::size_t last = start;
    for (std::size_t i = start; i < size && i - start < MaxRunScan; ++i)
    {
        RunPoint const p = at(i);
        if (EndsRun(p, id.ride))
            break;
        if (!id.ride && p.entry)
        {
            id.ride = p.entry;
            id.rideOff = i - start;
        }
        last = i;
        ++id.runLen;
    }
    RunPoint const end = at(last);
    id.toMap = end.map;
    id.toX = end.x;
    id.toY = end.y;
    return id;
}

// The per-tick check of a cached run, O(1): the handed point, the ride point, the last point and the point after the run
// are where the scan found them (exact compares). The points in between do not enter the key.
template <class At>
bool RunStill(RunId const& id, std::size_t start, std::size_t size, At const& at)
{
    if (start >= size)
        return false;
    RunPoint const first = at(start);
    if (first.map != id.map || first.x != id.x || first.y != id.y || first.entry != id.hop)
        return false;
    if (id.rideOff != NoRide)
    {
        if (start + id.rideOff >= size)
            return false;
        RunPoint const r = at(start + id.rideOff);
        if (!r.transport || r.entry != id.ride)
            return false;
    }
    std::size_t const last = start + (id.runLen ? id.runLen - 1 : 0);
    if (last >= size)
        return false;
    RunPoint const end = at(last);
    if (end.map != id.toMap || end.x != id.toX || end.y != id.toY)
        return false;
    if (id.runLen && id.runLen < MaxRunScan && last + 1 < size && !EndsRun(at(last + 1), id.ride))
        return false;
    return true;
}

// The key fields a run gives; the caller adds lift and the TaxiPath direction (Derive).
inline Key KeyOf(RunId const& id, bool minimal)
{
    Key key;
    key.valid = true;
    key.ride = id.ride;
    key.hop = id.hop;
    key.minimal = minimal;
    key.fromMap = id.map;
    key.fromX = id.x;
    key.fromY = id.y;
    key.toMap = id.toMap;
    key.toX = id.toX;
    key.toY = id.toY;
    return key;
}

enum class Phase : std::uint8_t { None, Attempt, Board };

// One episode of one bot (bot thread only, in LastMovement).
struct Episode
{
    Phase phase = Phase::None;
    Key key;
    std::uint32_t waitStartMs = 0;
    std::uint32_t lastSeenMs = 0;   // last wait / boarding / aboard tick: a wait or ride left without a hook ends here
    std::uint32_t boardMs = 0;
    bool timeoutLogged = false;
    bool overrunLogged = false;
    bool boardPointLogged = false;
};

// Per-bot state: the episode plus the callers' key cache (two runs, so a dock node / stop node handover that alternates
// does not re-read the TaxiPath) and which key the current hook uses.
struct State
{
    struct Slot
    {
        bool set = false;
        RunId id;
        Key key;
    };

    static constexpr std::int8_t NoKey = -1;
    static constexpr std::int8_t EpisodeKey = 2;   // aboard: the episode's own key

    Episode episode;
    Slot cache[2];
    std::uint8_t lastUsed = 0;
    std::int8_t current = NoKey;   // 0 / 1 = cache slot, EpisodeKey; an index, so a copy of the state stays valid

    bool Active() const { return cache[0].set || cache[1].set || episode.phase != Phase::None; }

    Key const& Current() const { return current == 0 || current == 1 ? cache[current].key : episode.key; }

    // A cached slot whose run is still the path's run from start: use it (no scan, no TaxiPath read).
    template <class At>
    bool UseCached(std::size_t start, std::size_t size, At const& at, bool minimal)
    {
        for (std::uint8_t i = 0; i < 2; ++i)
        {
            if (cache[i].set && RunStill(cache[i].id, start, size, at))
            {
                cache[i].key.minimal = minimal;
                lastUsed = i;
                current = std::int8_t(i);
                return true;
            }
        }
        return false;
    }

    // A newly derived key: replaces the slot used less recently and becomes current.
    void Store(RunId const& id, Key const& key)
    {
        std::uint8_t const slot = !cache[0].set ? 0 : !cache[1].set ? 1 : std::uint8_t(1 - lastUsed);
        cache[slot].set = true;
        cache[slot].id = id;
        cache[slot].key = key;
        lastUsed = slot;
        current = std::int8_t(slot);
    }
};

// What one hook call has to log (Render), in this order.
struct Step
{
    bool routeChanged = false;   // fail reason=route_changed for `previous` (attempted, never boarded, another edge now)
    bool left = false;           // fail reason=left for `previous`: wait or ride left without a hook (gap / not aboard)
    bool leftAboard = false;     // the left episode had boarded
    Key previous;
    std::uint32_t previousWaitMs = 0;
    std::uint32_t previousRideMs = 0;
    bool attempt = false;
    bool noRide = false;         // fail reason=no_ride: the run has no ride entry (once per episode)
    bool waitTimeout = false;
    bool board = false;
    bool boardPoint = false;
    bool arrive = false;
    bool exitOrigin = false;     // fail reason=exit_origin: the aboard exit fired at the departure side
    bool overrun = false;
    bool fellOff = false;
    bool stallAbandon = false;
    bool aboard = false;         // the episode had boarded when it ended (stall_abandon)
    std::uint32_t waitMs = 0;
    std::uint32_t rideMs = 0;

    bool Any() const
    {
        return routeChanged || left || attempt || noRide || waitTimeout || board || boardPoint || arrive || exitOrigin ||
            overrun || fellOff || stallAbandon;
    }
};

inline std::uint32_t Since(std::uint32_t nowMs, std::uint32_t thenMs) { return nowMs - thenMs; }   // wraps like getMSTimeDiff

// A new episode when none runs, the key changed, the old wait was left (gap), or a boarded episode is no longer aboard
// (Begin only runs off the vessel). The old episode is closed first: route_changed (another edge within the gap) or left.
inline void Begin(State& state, Key const& passed, std::uint32_t nowMs, Step& step)
{
    Key const key = passed;   // a copy: `passed` may point into the state
    Episode& ep = state.episode;
    bool const gap = ep.phase != Phase::None && Since(nowMs, ep.lastSeenMs) > GapMs;
    if (ep.phase == Phase::Attempt && !gap && SameEdge(ep.key, key))
        return;
    if (ep.phase == Phase::Attempt && !gap)
    {
        step.routeChanged = true;
        step.previous = ep.key;
        step.previousWaitMs = Since(ep.lastSeenMs, ep.waitStartMs);   // observed wait, not the time since
    }
    else if (ep.phase != Phase::None)
    {
        step.left = true;
        step.leftAboard = ep.phase == Phase::Board;
        step.previous = ep.key;
        step.previousWaitMs = step.leftAboard ? Since(ep.boardMs, ep.waitStartMs) : Since(ep.lastSeenMs, ep.waitStartMs);
        step.previousRideMs = step.leftAboard ? Since(ep.lastSeenMs, ep.boardMs) : 0;
    }
    ep = Episode();
    ep.phase = Phase::Attempt;
    ep.key = key;
    ep.waitStartMs = nowMs;
    ep.lastSeenMs = nowMs;
    step.attempt = true;
    step.noRide = !key.ride;   // once per episode: only a new episode gets here
}

// UseTransport's not-boardable exit (one call per wait tick).
inline Step OnWait(State& state, Key const& key, std::uint32_t nowMs, std::uint32_t timeoutMs)
{
    Step step;
    Begin(state, key, nowMs, step);
    Episode& ep = state.episode;
    ep.lastSeenMs = nowMs;
    if (!ep.timeoutLogged && Since(nowMs, ep.waitStartMs) >= timeoutMs)
    {
        step.waitTimeout = true;
        step.waitMs = Since(nowMs, ep.waitStartMs);
        ep.timeoutLogged = true;   // the key stays: later ticks of this wait are silent
    }
    return step;
}

// Right after MoveOnTransport: boarded = the bot has a transport now; failed = MoveOnTransport did not board and is not
// walking there (UseTransport still returns true: fail reason=board_point, once per episode).
inline Step OnBoardTry(State& state, Key const& key, std::uint32_t nowMs, bool boarded, bool failed)
{
    Step step;
    Begin(state, key, nowMs, step);
    Episode& ep = state.episode;
    ep.lastSeenMs = nowMs;
    step.waitMs = Since(nowMs, ep.waitStartMs);
    if (boarded)
    {
        step.board = true;
        ep.phase = Phase::Board;
        ep.boardMs = nowMs;
    }
    else if (failed && !ep.boardPointLogged)
    {
        step.boardPoint = true;
        ep.boardPointLogged = true;
    }
    return step;
}

// The aboard exit (MoveOffTransport called): ends the episode as arrive, or as fail reason=exit_origin when the bot got
// off at the departure side (`side` from ExitSide on the position before the exit).
inline Step OnArrive(State& state, std::uint32_t nowMs, Side side)
{
    Step step;
    Episode& ep = state.episode;
    if (ep.phase != Phase::Board)
        return step;
    step.arrive = side != Side::From;
    step.exitOrigin = side == Side::From;
    step.aboard = true;
    step.waitMs = Since(ep.boardMs, ep.waitStartMs);
    step.rideMs = Since(nowMs, ep.boardMs);
    step.previous = ep.key;
    ep = Episode();
    return step;
}

// The aboard wait (one call per tick on the vessel): overrun once when aboard longer than the threshold.
inline Step OnAboard(State& state, std::uint32_t nowMs, std::uint32_t timeoutMs)
{
    Step step;
    Episode& ep = state.episode;
    if (ep.phase != Phase::Board)
        return step;
    ep.lastSeenMs = nowMs;
    if (ep.overrunLogged || Since(nowMs, ep.boardMs) < timeoutMs)
        return step;
    step.overrun = true;
    step.aboard = true;
    step.waitMs = Since(ep.boardMs, ep.waitStartMs);
    step.rideMs = Since(nowMs, ep.boardMs);
    ep.overrunLogged = true;
    return step;
}

// PlayerbotAI removed the bot from a vessel it no longer stands on (or that its master is not on): ends a boarded episode.
inline Step OnFellOff(State& state, std::uint32_t nowMs)
{
    Step step;
    Episode& ep = state.episode;
    if (ep.phase != Phase::Board)
        return step;
    step.fellOff = true;
    step.aboard = true;
    step.waitMs = Since(ep.boardMs, ep.waitStartMs);
    step.rideMs = Since(nowMs, ep.boardMs);
    step.previous = ep.key;
    ep = Episode();
    return step;
}

// TravelMgr's transport_stall abandon: ends any running episode.
inline Step OnStallAbandon(State& state, std::uint32_t nowMs)
{
    Step step;
    Episode& ep = state.episode;
    if (ep.phase == Phase::None)
        return step;
    step.stallAbandon = true;
    step.aboard = ep.phase == Phase::Board;
    step.waitMs = step.aboard ? Since(ep.boardMs, ep.waitStartMs) : Since(ep.lastSeenMs, ep.waitStartMs);
    step.rideMs = step.aboard ? Since(nowMs, ep.boardMs) : 0;
    step.previous = ep.key;
    ep = Episode();
    return step;
}

// Shared by all map threads: unsampled counts, own line budget, a summary per minute with transitions (TaxiCounters
// pattern). minute / minutes: the window the counts cover, [minute, minute + minutes) in epoch minutes.
struct Snapshot
{
    std::uint32_t attempt = 0, board = 0, arrive = 0, fail = 0, failEnd = 0, dropped = 0;
    std::uint64_t waitMsSum = 0, rideMsSum = 0;
    std::uint64_t minute = 0, minutes = 0;
};

struct Counters
{
    std::atomic<std::uint32_t> attempt{0};
    std::atomic<std::uint32_t> board{0};
    std::atomic<std::uint32_t> arrive{0};
    std::atomic<std::uint32_t> fail{0};
    std::atomic<std::uint32_t> failEnd{0};
    std::atomic<std::uint32_t> dropped{0};
    std::atomic<std::uint32_t> logged{0};
    std::atomic<std::uint64_t> waitMsSum{0};
    std::atomic<std::uint64_t> rideMsSum{0};
    std::atomic<std::uint64_t> minute{0};

    // One caller per new minute gets the previous window (none for the very first).
    bool Roll(std::uint64_t nowSeconds, Snapshot& out)
    {
        std::uint64_t const now = nowSeconds / 60;
        std::uint64_t seen = minute.load(std::memory_order_relaxed);
        if (seen == now || !minute.compare_exchange_strong(seen, now, std::memory_order_relaxed))
            return false;
        out.attempt = attempt.exchange(0, std::memory_order_relaxed);
        out.board = board.exchange(0, std::memory_order_relaxed);
        out.arrive = arrive.exchange(0, std::memory_order_relaxed);
        out.fail = fail.exchange(0, std::memory_order_relaxed);
        out.failEnd = failEnd.exchange(0, std::memory_order_relaxed);
        out.dropped = dropped.exchange(0, std::memory_order_relaxed);
        out.waitMsSum = waitMsSum.exchange(0, std::memory_order_relaxed);
        out.rideMsSum = rideMsSum.exchange(0, std::memory_order_relaxed);
        out.minute = seen;
        out.minutes = now - seen;
        logged.exchange(0, std::memory_order_relaxed);
        return seen != 0;
    }

    // Every transition is counted; the sums follow the rule in the header comment (wait time when a wait ends: board or
    // an ending fail with phase=wait; ride time when a ride ends: arrive or an ending fail with phase=ride).
    void Account(Step const& step)
    {
        if (step.attempt)
            attempt.fetch_add(1, std::memory_order_relaxed);
        if (step.board)
            board.fetch_add(1, std::memory_order_relaxed);
        if (step.arrive)
            arrive.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t const ends = std::uint32_t(step.routeChanged) + std::uint32_t(step.left) +
            std::uint32_t(step.exitOrigin) + std::uint32_t(step.fellOff) + std::uint32_t(step.stallAbandon);
        std::uint32_t const fails = ends + std::uint32_t(step.noRide) + std::uint32_t(step.waitTimeout) +
            std::uint32_t(step.boardPoint) + std::uint32_t(step.overrun);
        if (fails)
            fail.fetch_add(fails, std::memory_order_relaxed);
        if (ends)
            failEnd.fetch_add(ends, std::memory_order_relaxed);
        std::uint64_t wait = 0, ride = 0;
        if (step.board)
            wait += step.waitMs;
        if (step.routeChanged || (step.left && !step.leftAboard))
            wait += step.previousWaitMs;
        if (step.left && step.leftAboard)
            ride += step.previousRideMs;
        if (step.stallAbandon && !step.aboard)
            wait += step.waitMs;
        if (step.arrive || step.exitOrigin || step.fellOff || (step.stallAbandon && step.aboard))
            ride += step.rideMs;
        if (wait)
            waitMsSum.fetch_add(wait, std::memory_order_relaxed);
        if (ride)
            rideMsSum.fetch_add(ride, std::memory_order_relaxed);
    }

    // One more line; true while within this minute's budget, else counted as dropped. No give-back of the increment: a
    // fetch_sub racing Roll's reset would wrap `logged` and drop the rest of the minute (a uint32 cannot overflow in one
    // minute of lines).
    bool Take()
    {
        if (logged.fetch_add(1, std::memory_order_relaxed) < LinesPerMinute)
            return true;
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }

// Line fields the state machine does not know.
struct BotInfo
{
    std::uint32_t guid = 0;
    std::uint32_t level = 0;
    char team = 'H';
    std::uint32_t atMap = 0;       // where the bot is at the transition (arrive / exit_origin: before MoveOffTransport)
    float atX = 0.0f;
    float atY = 0.0f;
};

struct Extra
{
    std::uint32_t cand = 0;        // candidate vessels getTransports returned
    float nearD2d = -1.0f;         // 2D distance nearest vessel -> handed point, -1 = none
    std::uint32_t got = 0;         // entry of the vessel the bot is on after boarding
    bool tele = false;             // boarding by teleport (TransportTeleportType > 0 / MinimalMove)
    bool ok = false;               // MoveOffTransport's return value
    float dExit = -1.0f;           // 2D distance bot -> to point right after MoveOffTransport, -1 = other map
    Side side = Side::Unknown;     // ExitSide of the arrive
    PointRejects rejects;          // RandomPointOnTrans rejects of the failed boarding
};

constexpr std::size_t MaxLinesPerStep = 4;

// The format strings are literals at the snprintf calls, so -Wformat checks them against the arguments.
inline std::string FormatAttempt(BotInfo const& bot, Key const& key, Extra const& extra)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "[TransportWait] state=attempt bot=%u level=%u team=%c edge=%s ride=%u hop=%u map=%u from_x=%.1f from_y=%.1f to_map=%u "
        "to_x=%.1f to_y=%.1f cand=%u near_d2d=%.1f minimal=%u ride_s_formula=%.6g",
        bot.guid, bot.level, bot.team, EdgeName(key).c_str(), key.ride, key.hop, key.fromMap, key.fromX, key.fromY, key.toMap,
        key.toX, key.toY, extra.cand, extra.nearD2d, key.minimal ? 1u : 0u, double(key.rideS));
    return buf;
}

inline std::string FormatBoard(BotInfo const& bot, Key const& key, Extra const& extra, std::uint32_t waitMs)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "[TransportWait] state=board bot=%u edge=%s ride=%u got=%u map=%u from_x=%.1f from_y=%.1f wait_ms=%u mode=%s",
        bot.guid, EdgeName(key).c_str(), key.ride, extra.got, key.fromMap, key.fromX, key.fromY, waitMs,
        extra.tele ? "tele" : "walk");
    return buf;
}

inline std::string FormatArrive(BotInfo const& bot, Key const& key, Extra const& extra, std::uint32_t rideMs)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "[TransportWait] state=arrive bot=%u edge=%s ride=%u to_map=%u to_x=%.1f to_y=%.1f ride_ms=%u ok=%u d_exit=%.1f "
        "side=%s at_map=%u at_x=%.1f at_y=%.1f",
        bot.guid, EdgeName(key).c_str(), key.ride, key.toMap, key.toX, key.toY, rideMs, extra.ok ? 1u : 0u, extra.dExit,
        SideName(extra.side), bot.atMap, bot.atX, bot.atY);
    return buf;
}

// phase / end: see the header comment. reason board_point carries the RandomPointOnTrans rejects at the end.
inline std::string FormatFail(BotInfo const& bot, Key const& key, char const* reason, std::uint32_t waitMs, std::uint32_t rideMs,
    bool aboard, bool ends, PointRejects const* rejects = nullptr)
{
    char buf[512];
    int const n = std::snprintf(buf, sizeof(buf),
        "[TransportWait] state=fail bot=%u edge=%s ride=%u map=%u from_x=%.1f from_y=%.1f reason=%s wait_ms=%u ride_ms=%u "
        "phase=%s end=%u at_map=%u at_x=%.1f at_y=%.1f",
        bot.guid, EdgeName(key).c_str(), key.ride, key.fromMap, key.fromX, key.fromY, reason, waitMs, rideMs,
        aboard ? "ride" : "wait", ends ? 1u : 0u, bot.atMap, bot.atX, bot.atY);
    if (rejects && n > 0 && std::size_t(n) < sizeof(buf))
        std::snprintf(buf + n, sizeof(buf) - std::size_t(n), " rp=z:%u/on:%u/path:%u", rejects->z, rejects->on, rejects->path);
    return buf;
}

inline std::string FormatSummary(Snapshot const& s)
{
    char buf[320];
    std::snprintf(buf, sizeof(buf),
        "[TransportWait] summary attempt=%u board=%u arrive=%u fail=%u fail_end=%u dropped=%u wait_ms_sum=%llu ride_ms_sum=%llu "
        "min=%llu mins=%llu",
        s.attempt, s.board, s.arrive, s.fail, s.failEnd, s.dropped, static_cast<unsigned long long>(s.waitMsSum),
        static_cast<unsigned long long>(s.rideMsSum), static_cast<unsigned long long>(s.minute),
        static_cast<unsigned long long>(s.minutes));
    return buf;
}

// The lines of one step, in transition order; `key` is the episode key after the step (the previous key for the
// route_changed / left, arrive / exit_origin, fell_off and stall_abandon lines comes from the step).
inline std::size_t Render(Step const& step, BotInfo const& bot, Key const& key, Extra const& extra,
    std::string (&out)[MaxLinesPerStep])
{
    std::size_t n = 0;
    auto add = [&](std::string line) { if (n < MaxLinesPerStep) out[n++] = std::move(line); };
    if (step.routeChanged)
        add(FormatFail(bot, step.previous, "route_changed", step.previousWaitMs, 0, false, true));
    if (step.left)
        add(FormatFail(bot, step.previous, "left", step.previousWaitMs, step.previousRideMs, step.leftAboard, true));
    if (step.attempt)
        add(FormatAttempt(bot, key, extra));
    if (step.noRide)
        add(FormatFail(bot, key, "no_ride", 0, 0, false, false));
    if (step.waitTimeout)
        add(FormatFail(bot, key, "wait_timeout", step.waitMs, 0, false, false));
    if (step.board)
        add(FormatBoard(bot, key, extra, step.waitMs));
    if (step.boardPoint)
        add(FormatFail(bot, key, "board_point", step.waitMs, 0, false, false, &extra.rejects));
    if (step.overrun)
        add(FormatFail(bot, key, "overrun", step.waitMs, step.rideMs, true, false));
    if (step.arrive)
        add(FormatArrive(bot, step.previous, extra, step.rideMs));
    if (step.exitOrigin)
        add(FormatFail(bot, step.previous, "exit_origin", step.waitMs, step.rideMs, true, true));
    if (step.fellOff)
        add(FormatFail(bot, step.previous, "fell_off", step.waitMs, step.rideMs, true, true));
    if (step.stallAbandon)
        add(FormatFail(bot, step.previous, "stall_abandon", step.waitMs, step.rideMs, step.aboard, true));
    return n;
}
}
