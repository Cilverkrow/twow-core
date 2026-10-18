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
// bot's transport edge episode (attempt -> board -> arrive, or a fail), never per tick, plus a minute summary with the
// unsampled counts. Measurement only: nothing here changes where a bot goes or when it boards. Pure: no game includes;
// the game side (MovementActions.cpp) fills Key / Extra and logs what Render returns.

// Own line budget, separate from the RouteFailDetail.LinesPerMinute budget the [TaxiUse] / old [TransportWait] lines share.
constexpr std::uint32_t LinesPerMinute = 120;

// AiPlayerbot.Path.TransportWaitTimeoutMin: the wait_timeout / overrun threshold of this trace, shared with the later
// transport wait switch (CLI-PATH S3b). The longest vessel period is 356,284 ms, so 15 min is about 2.5 round trips.
constexpr std::int32_t DefaultWaitTimeoutMin = 15;
constexpr std::int32_t MinWaitTimeoutMin = 1;
constexpr std::int32_t MaxWaitTimeoutMin = 240;

inline std::uint32_t ClampWaitTimeoutMin(std::int32_t minutes)
{
    if (minutes < MinWaitTimeoutMin)
        return std::uint32_t(MinWaitTimeoutMin);
    if (minutes > MaxWaitTimeoutMin)
        return std::uint32_t(MaxWaitTimeoutMin);
    return std::uint32_t(minutes);
}

inline std::uint32_t TimeoutMs(std::uint32_t minutes) { return minutes * 60u * 1000u; }

// Caller bounds: the NODE_TRANSPORT run scanned for the ride entry (dock hop + ride leg + dock hop) and the TaxiPath nodes.
constexpr std::size_t MaxRunScan = 256;
constexpr std::size_t MaxTaxiNodes = 1024;
// A from / to point names a stop only within this distance (dock nodes are 0-16 yd from their stop) and only when the
// second-nearest stop is at least AmbiguousMarginYards farther away (CLI-PATH: no guessing, '-' instead).
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

// The edge of one transport run: computed by the caller once per change of the handed point.
struct Key
{
    bool valid = false;
    std::uint32_t ride = 0;     // first non-zero entry in the NODE_TRANSPORT run (the ride leg's GO entry), 0 = none
    std::uint32_t hop = 0;      // entry of the point handed to UseTransport (0 for an unfilled dock hop)
    std::int32_t dirFrom = -1;  // stop index (0-based, merged stops in TaxiPath order) of the from / to point, -1 = unknown
    std::int32_t dirTo = -1;
    std::uint32_t stops = 0;    // stops of the TaxiPath after merging, 0 = no TaxiPath (elevator, tram, unknown entry)
    std::uint32_t fromMap = 0;  // the handed point (where the bot boards)
    float fromX = 0.0f;
    float fromY = 0.0f;
    std::uint32_t toMap = 0;    // last point of the run (where the bot gets off)
    float toX = 0.0f;
    float toY = 0.0f;
    float cost = -1.0f;         // ride leg cost (see RideCost), -1 = unknown
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

// The transport branch of TravelNodePath::getCost is extraCost * modifier (modifier 1 for transport links); the generator
// sets a ship leg's extraCost to its TaxiPath length / moTransport.moveSpeed (setPathAndCost). The trace cannot read the A*
// link without a node lookup, so it reports this formula value for the leg stop i (departure node) -> stop j (arrival
// node) in path order, wrapping,
// same-map segments only. The shipped graph may store another value (0.003333 on 14 legs, i342 map V3); the parser
// joins the stored one from the SQL. -1 = unknown.
inline float RideCost(std::vector<PathNode> const& nodes, std::vector<Stop> const& stops, std::int32_t from, std::int32_t to,
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

// Direction and cost of a key from the vessel's TaxiPath nodes (empty = no TaxiPath).
inline void Derive(Key& key, std::vector<PathNode> const& nodes, float moveSpeed)
{
    std::vector<Stop> const stops = StopsOf(nodes);
    key.stops = std::uint32_t(stops.size());
    key.dirFrom = NearestStop(stops, key.fromMap, key.fromX, key.fromY);
    key.dirTo = NearestStop(stops, key.toMap, key.toX, key.toY);
    key.cost = RideCost(nodes, stops, key.dirFrom, key.dirTo, moveSpeed);
}

// "AB" / "BA" for a two-stop path (A = first delay>0 stop), "<i>-<j>" for more stops (stop ordinals from 1 after merging,
// CLI-PATH stop rule), "-" without a path or a clear match.
inline std::string DirName(Key const& key)
{
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
    if (!a.valid || !b.valid || a.ride != b.ride || a.dirFrom != b.dirFrom || a.dirTo != b.dirTo)
        return false;
    if (a.ride && a.dirFrom >= 0 && a.dirTo >= 0)
        return true;
    if (a.fromMap != b.fromMap)
        return false;
    float const dx = a.fromX - b.fromX, dy = a.fromY - b.fromY;
    return dx * dx + dy * dy <= SamePointYards * SamePointYards;
}

// RandomPointOnTrans rejects (optional out-param, default nullptr there): z window, isOnTransport, walk path.
struct PointRejects
{
    std::uint32_t z = 0;
    std::uint32_t on = 0;
    std::uint32_t path = 0;
};

enum class Phase : std::uint8_t { None, Attempt, Board };

// One episode of one bot (bot thread only, in LastMovement).
struct Episode
{
    Phase phase = Phase::None;
    Key key;
    std::uint32_t waitStartMs = 0;
    std::uint32_t lastSeenMs = 0;   // last wait tick / boarding try: a wait the bot left without a hook ends here
    std::uint32_t boardMs = 0;
    bool timeoutLogged = false;
    bool overrunLogged = false;
    bool boardPointLogged = false;
};

// Per-bot state: the episode plus the caller's cache (the handed point its key was derived from).
struct State
{
    Episode episode;
    bool sourceSet = false;
    std::uint32_t sourceMap = 0;
    std::uint32_t sourceEntry = 0;
    float sourceX = 0.0f;
    float sourceY = 0.0f;
    Key sourceKey;

    bool Active() const { return sourceSet || episode.phase != Phase::None; }

    // The per-tick check of the callers: exact compare, the key scan runs only when this is false.
    bool SourceIs(std::uint32_t map, float x, float y, std::uint32_t entry) const
    {
        return sourceSet && sourceMap == map && sourceX == x && sourceY == y && sourceEntry == entry;
    }

    void SetSource(std::uint32_t map, float x, float y, std::uint32_t entry, Key const& key)
    {
        sourceSet = true;
        sourceMap = map;
        sourceX = x;
        sourceY = y;
        sourceEntry = entry;
        sourceKey = key;
    }
};

// What one hook call has to log (Render), in this order.
struct Step
{
    bool routeChanged = false;   // fail reason=route_changed for `previous` (attempted, never boarded)
    Key previous;
    std::uint32_t previousWaitMs = 0;
    bool attempt = false;
    bool noRide = false;         // fail reason=no_ride: the run has no ride entry (once per episode)
    bool waitTimeout = false;
    bool board = false;
    bool boardPoint = false;
    bool arrive = false;
    bool overrun = false;
    bool fellOff = false;
    bool stallAbandon = false;
    bool aboard = false;         // the episode was aboard when it ended (stall_abandon)
    std::uint32_t waitMs = 0;
    std::uint32_t rideMs = 0;

    bool Any() const
    {
        return routeChanged || attempt || noRide || waitTimeout || board || boardPoint || arrive || overrun || fellOff ||
            stallAbandon;
    }
};

inline std::uint32_t Since(std::uint32_t nowMs, std::uint32_t thenMs) { return nowMs - thenMs; }   // wraps like getMSTimeDiff

// A new episode when none runs, the bot is no longer aboard one, or the key changed.
inline void Begin(State& state, Key const& passed, std::uint32_t nowMs, Step& step)
{
    Key const key = passed;   // a copy: `passed` may point into the state
    Episode& ep = state.episode;
    if (ep.phase == Phase::Attempt && SameEdge(ep.key, key))
        return;
    if (ep.phase == Phase::Attempt)
    {
        step.routeChanged = true;
        step.previous = ep.key;
        step.previousWaitMs = Since(ep.lastSeenMs, ep.waitStartMs);   // observed wait, not the time since
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

// The aboard exit (MoveOffTransport called): ends the episode.
inline Step OnArrive(State& state, std::uint32_t nowMs)
{
    Step step;
    Episode& ep = state.episode;
    if (ep.phase != Phase::Board)
        return step;
    step.arrive = true;
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
    if (ep.phase != Phase::Board || ep.overrunLogged || Since(nowMs, ep.boardMs) < timeoutMs)
        return step;
    step.overrun = true;
    step.waitMs = Since(ep.boardMs, ep.waitStartMs);
    step.rideMs = Since(nowMs, ep.boardMs);
    ep.overrunLogged = true;
    return step;
}

// PlayerbotAI removed the bot from a vessel it no longer stands on: ends a boarded episode.
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

// Shared by all map threads: unsampled counts, own line budget, a summary once a minute (TaxiCounters pattern).
struct Snapshot
{
    std::uint32_t attempt = 0, board = 0, arrive = 0, fail = 0, dropped = 0;
    std::uint64_t waitMsSum = 0, rideMsSum = 0;
};

struct Counters
{
    std::atomic<std::uint32_t> attempt{0};
    std::atomic<std::uint32_t> board{0};
    std::atomic<std::uint32_t> arrive{0};
    std::atomic<std::uint32_t> fail{0};
    std::atomic<std::uint32_t> dropped{0};
    std::atomic<std::uint32_t> logged{0};
    std::atomic<std::uint64_t> waitMsSum{0};
    std::atomic<std::uint64_t> rideMsSum{0};
    std::atomic<std::uint64_t> minute{0};

    // One caller per new minute gets the previous minute (none for the very first).
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
        out.dropped = dropped.exchange(0, std::memory_order_relaxed);
        out.waitMsSum = waitMsSum.exchange(0, std::memory_order_relaxed);
        out.rideMsSum = rideMsSum.exchange(0, std::memory_order_relaxed);
        logged.exchange(0, std::memory_order_relaxed);
        return seen != 0;
    }

    // Every transition is counted; wait time is added when a wait ends (board, route change, abandon before boarding),
    // ride time when a ride ends (arrive, fell off, abandon aboard).
    void Account(Step const& step)
    {
        if (step.attempt)
            attempt.fetch_add(1, std::memory_order_relaxed);
        if (step.board)
            board.fetch_add(1, std::memory_order_relaxed);
        if (step.arrive)
            arrive.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t const fails = std::uint32_t(step.routeChanged) + std::uint32_t(step.noRide) +
            std::uint32_t(step.waitTimeout) + std::uint32_t(step.boardPoint) + std::uint32_t(step.overrun) +
            std::uint32_t(step.fellOff) + std::uint32_t(step.stallAbandon);
        if (fails)
            fail.fetch_add(fails, std::memory_order_relaxed);
        std::uint64_t wait = 0, ride = 0;
        if (step.board)
            wait += step.waitMs;
        if (step.routeChanged)
            wait += step.previousWaitMs;
        if (step.stallAbandon && !step.aboard)
            wait += step.waitMs;
        if (step.arrive || step.fellOff || (step.stallAbandon && step.aboard))
            ride += step.rideMs;
        if (wait)
            waitMsSum.fetch_add(wait, std::memory_order_relaxed);
        if (ride)
            rideMsSum.fetch_add(ride, std::memory_order_relaxed);
    }

    // One more line; true while within this minute's budget, else counted as dropped.
    bool Take()
    {
        if (logged.fetch_add(1, std::memory_order_relaxed) < LinesPerMinute)
            return true;
        logged.fetch_sub(1, std::memory_order_relaxed);
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
};

struct Extra
{
    std::uint32_t cand = 0;        // candidate vessels getTransports returned
    float nearD2d = -1.0f;         // 2D distance nearest vessel -> handed point, -1 = none
    std::uint32_t got = 0;         // entry of the vessel the bot is on after boarding
    bool tele = false;             // boarding by teleport (TransportTeleportType > 0 / MinimalMove)
    bool ok = false;               // MoveOffTransport's return value
    float dExit = -1.0f;           // 2D distance bot -> to point right after MoveOffTransport, -1 = other map
    PointRejects rejects;          // RandomPointOnTrans rejects of the failed boarding
};

constexpr std::size_t MaxLinesPerStep = 4;

inline char const* const kAttempt =
    "[TransportWait] state=attempt bot=%u level=%u team=%c edge=%s ride=%u hop=%u map=%u from_x=%.1f from_y=%.1f to_map=%u "
    "to_x=%.1f to_y=%.1f cand=%u near_d2d=%.1f minimal=%u cost=%.6g";
inline char const* const kBoard =
    "[TransportWait] state=board bot=%u edge=%s ride=%u got=%u map=%u from_x=%.1f from_y=%.1f wait_ms=%u mode=%s";
inline char const* const kArrive =
    "[TransportWait] state=arrive bot=%u edge=%s ride=%u to_map=%u to_x=%.1f to_y=%.1f ride_ms=%u ok=%u d_exit=%.1f";
inline char const* const kFail =
    "[TransportWait] state=fail bot=%u edge=%s ride=%u map=%u from_x=%.1f from_y=%.1f reason=%s wait_ms=%u ride_ms=%u";
inline char const* const kSummary =
    "[TransportWait] summary attempt=%u board=%u arrive=%u fail=%u dropped=%u wait_ms_sum=%llu ride_ms_sum=%llu";

inline std::string FormatAttempt(BotInfo const& bot, Key const& key, Extra const& extra)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), kAttempt, bot.guid, bot.level, bot.team, EdgeName(key).c_str(), key.ride, key.hop,
        key.fromMap, key.fromX, key.fromY, key.toMap, key.toX, key.toY, extra.cand, extra.nearD2d, key.minimal ? 1u : 0u,
        double(key.cost));
    return buf;
}

inline std::string FormatBoard(BotInfo const& bot, Key const& key, Extra const& extra, std::uint32_t waitMs)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), kBoard, bot.guid, EdgeName(key).c_str(), key.ride, extra.got, key.fromMap, key.fromX,
        key.fromY, waitMs, extra.tele ? "tele" : "walk");
    return buf;
}

inline std::string FormatArrive(BotInfo const& bot, Key const& key, Extra const& extra, std::uint32_t rideMs)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), kArrive, bot.guid, EdgeName(key).c_str(), key.ride, key.toMap, key.toX, key.toY, rideMs,
        extra.ok ? 1u : 0u, extra.dExit);
    return buf;
}

// reason board_point carries the RandomPointOnTrans rejects at the end.
inline std::string FormatFail(BotInfo const& bot, Key const& key, char const* reason, std::uint32_t waitMs, std::uint32_t rideMs,
    PointRejects const* rejects = nullptr)
{
    char buf[512];
    int const n = std::snprintf(buf, sizeof(buf), kFail, bot.guid, EdgeName(key).c_str(), key.ride, key.fromMap, key.fromX,
        key.fromY, reason, waitMs, rideMs);
    if (rejects && n > 0 && std::size_t(n) < sizeof(buf))
        std::snprintf(buf + n, sizeof(buf) - std::size_t(n), " rp=z:%u/on:%u/path:%u", rejects->z, rejects->on, rejects->path);
    return buf;
}

inline std::string FormatSummary(Snapshot const& s)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), kSummary, s.attempt, s.board, s.arrive, s.fail, s.dropped,
        static_cast<unsigned long long>(s.waitMsSum), static_cast<unsigned long long>(s.rideMsSum));
    return buf;
}

// The lines of one step, in transition order; `key` is the episode key after the step (the previous key for the
// route change, arrive, fell_off and stall_abandon lines comes from the step).
inline std::size_t Render(Step const& step, BotInfo const& bot, Key const& key, Extra const& extra,
    std::string (&out)[MaxLinesPerStep])
{
    std::size_t n = 0;
    auto add = [&](std::string line) { if (n < MaxLinesPerStep) out[n++] = std::move(line); };
    if (step.routeChanged)
        add(FormatFail(bot, step.previous, "route_changed", step.previousWaitMs, 0));
    if (step.attempt)
        add(FormatAttempt(bot, key, extra));
    if (step.noRide)
        add(FormatFail(bot, key, "no_ride", 0, 0));
    if (step.waitTimeout)
        add(FormatFail(bot, key, "wait_timeout", step.waitMs, 0));
    if (step.board)
        add(FormatBoard(bot, key, extra, step.waitMs));
    if (step.boardPoint)
        add(FormatFail(bot, key, "board_point", step.waitMs, 0, &extra.rejects));
    if (step.overrun)
        add(FormatFail(bot, key, "overrun", step.waitMs, step.rideMs));
    if (step.arrive)
        add(FormatArrive(bot, step.previous, extra, step.rideMs));
    if (step.fellOff)
        add(FormatFail(bot, step.previous, "fell_off", step.waitMs, step.rideMs));
    if (step.stallAbandon)
        add(FormatFail(bot, step.previous, "stall_abandon", step.waitMs, step.rideMs));
    return n;
}
}
