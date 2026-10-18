#pragma once

#include "TransportEdgePolicy.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::transport_board
{
// twow-repo#342 (S3b-0, no behaviour change): the S1 wait threshold from AiPlayerbot.Path.TransportWaitTimeoutMin, and the
// deck probe of AiPlayerbot.Path.TransportDeckProbe (default 0): once per dock wait, while the vessel stands at its stop
// near the dock or after a failed boarding, 8 fixed points of the vessel are probed (static hit, deck model short / long
// ray, map dynamic ray, model in the map tree) and one [TransportDeck] line is written (own budget) plus a
// [TransportBoard] summary. The probe answers nothing the boarding uses. Pure: the only include is S1's pure header; the
// game side (MovementActions.cpp, WorldPosition.cpp) fills DeckProbe and logs what the formats return.
// twow-repo#342 (S3b-1, AiPlayerbot.Path.TransportBoardFix, default 0): the predicates of the boarding fix below (board gate,
// ride record, exit at the stored arrival dock, the lost-ride recovery of OB-10 #1). With the switch off none is called.

// ---- D3: the wait timeout key; S1's fixed constant stays the definition of the default ----
constexpr std::uint32_t DefaultWaitTimeoutMin = 15;
constexpr std::uint32_t MaxWaitTimeoutMin = 240;

// <= 0 -> the default (15), at most MaxWaitTimeoutMin.
constexpr std::uint32_t ClampWaitTimeoutMin(std::int32_t minutes)
{
    if (minutes <= 0)
        return DefaultWaitTimeoutMin;
    return std::uint32_t(minutes) > MaxWaitTimeoutMin ? MaxWaitTimeoutMin : std::uint32_t(minutes);
}

// The S1 [TransportWait] wait_timeout / overrun threshold the callers pass (MovementActions.cpp); 0 -> the default, at
// most MaxWaitTimeoutMin (the config value is clamped already).
constexpr std::uint32_t WaitTimeoutMsOf(std::uint32_t minutes)
{
    return (minutes == 0 ? DefaultWaitTimeoutMin : minutes > MaxWaitTimeoutMin ? MaxWaitTimeoutMin : minutes) * 60000u;
}

// At the default the threshold is byte-identical to S1's fixed 900 s.
static_assert(WaitTimeoutMsOf(15) == transport_edge::WaitTimeoutMs, "S1 default threshold is 15 min");

// ---- the vessel at its stop near the dock (VesselAtStopNear in MovementActions.cpp) ----
// Dock node -> its own stop 0.60-15.94 yd (2D, dump nodes); nearest stop of another vessel >= 46.0 yd (1780 -> 1774).
constexpr float StopRadiusYards = 20.0f;

// Transport::Update's stop test (Transport.cpp:162): arrive <= progress < depart.
inline bool AtStopFrame(std::uint32_t progress, std::uint32_t arrive, std::uint32_t depart)
{
    return depart > arrive && progress >= arrive && progress < depart;
}

inline bool StopNear(float d2d) { return d2d >= 0.0f && d2d <= StopRadiusYards; }

// twow-repo#342 (S3b-1, CLI-Deep nit 18.10.): the stop test walks the vessel's key frames, so per bot it runs at most once
// per StopCheckMs for the same vessel and dock; the board gate, the exit gate and the deck probe share the answer.
constexpr std::uint32_t StopCheckMs = 3000;

struct StopCheck
{
    bool set = false;
    bool answer = false;
    std::uint32_t ride = 0;
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t ms = 0;
};

// The stored answer still holds: same vessel, same dock point, younger than StopCheckMs (wrap-safe).
inline bool StopCheckFresh(StopCheck const& c, std::uint32_t ride, std::uint32_t map, float x, float y, std::uint32_t nowMs)
{
    return c.set && c.ride == ride && c.map == map && c.x == x && c.y == y && transport_edge::Since(nowMs, c.ms) < StopCheckMs;
}

inline void NoteStopCheck(StopCheck& c, std::uint32_t ride, std::uint32_t map, float x, float y, std::uint32_t nowMs, bool answer)
{
    c.set = true;
    c.answer = answer;
    c.ride = ride;
    c.map = map;
    c.x = x;
    c.y = y;
    c.ms = nowMs;
}

// ---- S3b-1 D1: board / exit (AiPlayerbot.Path.TransportBoardFix) ----
// With the fix one boarding try per bot and ride at most this often (RandomPointOnTrans runs up to 100 tries each).
constexpr std::uint32_t BoardRetryMs = 5000;
// The stored exit point must lie this close (2D, same map) to the arrival dock, else the bot is put on the dock.
constexpr float MaxExitFromDockYards = 40.0f;
// OB-10 #1: a bot off its vessel this close (2D, same map) to the arrival dock or the exit continues after the run.
constexpr float RecoverNearYards = 40.0f;
// A path point is the ride's arrival dock: same entry, within this distance (2D, same map).
constexpr float SameDockYards = 1.0f;

// Boarding with the fix: a known entry, a handed point that does not end its run (never from the arrival side), the
// legacy 5 yd test or the vessel at its stop near the dock.
inline bool BoardOpen(std::uint32_t entry, bool runEnd, bool legacyNear, bool atStopNear)
{
    return entry != 0 && !runEnd && (legacyNear || atStopNear);
}

// The honest answer of a boarding try: aboard now, or (walk mode only) walking there. false = keep waiting.
inline bool Boarded(bool onVessel, bool doTeleport, bool moved) { return onVessel || (!doTeleport && moved); }

struct Point
{
    bool set = false;
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Point PointOf(std::uint32_t map, float x, float y, float z)
{
    Point p;
    p.set = true;
    p.map = map;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

inline bool Near2d(Point const& p, std::uint32_t map, float x, float y, float yards)
{
    float const dx = p.x - x, dy = p.y - y;
    return p.set && p.map == map && dx * dx + dy * dy <= yards * yards;
}

// A real point: set and not the (0,0,0) of an empty WorldPosition.
inline bool PointUsable(Point const& p) { return p.set && !(p.x == 0.0f && p.y == 0.0f && p.z == 0.0f); }

// The stored exit is used only when it is a real point on the dock's map within MaxExitFromDockYards of the dock.
inline bool ExitUsable(Point const& exit, Point const& dock)
{
    return PointUsable(exit) && PointUsable(dock) && Near2d(dock, exit.map, exit.x, exit.y, MaxExitFromDockYards);
}

// twow-repo#342 (S3b-1, review 18.10.): a walking bot (HandleSpecialMovement; UpcommingSpecialMovement hands it over only
// this close) boards only within BoardFromDockYards (2D, same map) of the handed dock; farther away it walks on, so the deck
// teleport never starts up to ReactDistance away. An unset walker point (MinimalMove's teleport steps, aboard) has no gate.
constexpr float BoardFromDockYards = StopRadiusYards;

inline bool WalkerNearDock(Point const& walker, Point const& dock)
{
    return !walker.set || Near2d(dock, walker.map, walker.x, walker.y, BoardFromDockYards);
}

// The run the handed point starts: its last point (arrival dock) and the point after it (exit; the last point if none).
struct Tail
{
    Point arrival;
    Point exit;
};

// Boarded by the fix: the vessel, where it stood when the bot boarded, and the tail of that run.
struct Ride
{
    bool set = false;
    std::uint32_t entry = 0;
    std::uint32_t boardMap = 0;
    float boardX = 0.0f;
    float boardY = 0.0f;
    Point arrival;
    Point exit;
};

inline Ride RideOf(std::uint32_t entry, std::uint32_t map, float x, float y, Tail const& tail)
{
    Ride ride;
    ride.set = true;
    ride.entry = entry;
    ride.boardMap = map;
    ride.boardX = x;
    ride.boardY = y;
    ride.arrival = tail.arrival;
    ride.exit = tail.exit;
    return ride;
}

// A path point is this ride's arrival dock.
inline bool IsArrival(Ride const& ride, bool transportPoint, std::uint32_t entry, std::uint32_t map, float x, float y)
{
    return ride.set && transportPoint && entry == ride.entry && Near2d(ride.arrival, map, x, y, SameDockYards);
}

// Side of the vessel now, between the boarding point (from) and the stored arrival dock (to): S1's ExitSide on that key.
inline transport_edge::Side RideSide(Ride const& ride, std::uint32_t atMap, float atX, float atY)
{
    transport_edge::Key key;
    key.valid = ride.set && ride.arrival.set;
    key.ride = ride.entry;
    key.fromMap = ride.boardMap;
    key.fromX = ride.boardX;
    key.fromY = ride.boardY;
    key.toMap = ride.arrival.map;
    key.toX = ride.arrival.x;
    key.toY = ride.arrival.y;
    return transport_edge::ExitSide(key, atMap, atX, atY);
}

// The exit of a fixed ride: only on the arrival side, at the arrival dock (legacy 5 yd or the vessel at its stop near it).
inline bool ExitOpen(transport_edge::Side side, bool legacyNear, bool atStopNear)
{
    return side == transport_edge::Side::To && (legacyNear || atStopNear);
}

// OB-10 #1 / #3: a ride record whose vessel the bot is no longer on (fell off, put off, teleported away). Only such an
// episode gets the recovery; a bot aboard without a record (master follow, a boarding without the fix) never does.
inline bool RideLost(Ride const& ride, bool aboard, std::uint32_t onEntry) { return ride.set && (!aboard || onEntry != ride.entry); }

// OB-10 #1 hybrid: near the arrival dock or the exit the bot continues after the run; else one route search from its own
// position; no route: the existing rescues (ZoneEscape, #587).
enum class Recovery : std::uint8_t { Near, Search, NoRoute };

inline bool RecoverNear(Ride const& ride, std::uint32_t map, float x, float y)
{
    return Near2d(ride.arrival, map, x, y, RecoverNearYards) || Near2d(ride.exit, map, x, y, RecoverNearYards);
}

inline char const* RecoveryName(Recovery r) { return r == Recovery::Near ? "near" : r == Recovery::Search ? "search" : "noroute"; }

// ---- S3b-1 D2: entry of a transport link's points; a 0-object hop takes the adjacent ride of the same route ----
inline std::uint32_t InheritEntry(std::uint32_t own, std::uint32_t nextRide, std::uint32_t prevRide)
{
    if (own)
        return own;
    if (nextRide && prevRide && nextRide != prevRide)
        return 0;   // two different rides around the hop: no guess
    return nextRide ? nextRide : prevRide;
}

// twow-repo#342 (S3b-1 D2, review 18.10.): a 0-object dock hop with no transport link next to it in the route (the route
// starts or ends at a stop node, or walk links on both sides) has no ride to board: its points are walked (NODE_PATH),
// so no run of entry 0 is ever waited on. A hop next to any transport link (a ride, a lift) keeps NODE_TRANSPORT.
inline bool WalkHop(std::uint32_t own, bool nextTransportLink, bool prevTransportLink)
{
    return !own && !nextTransportLink && !prevTransportLink;
}

// Per bot, in LastMovement (bot thread only). Written only with the fix on.
struct BotState
{
    // per call (BoardFor in MovementActions.cpp)
    bool runEnd = false;   // the handed point is the last point of its NODE_TRANSPORT run (no boarding from the arrival side)
    bool exited = false;   // this UseTransport call ended a fixed ride (the caller drops the run end)
    Tail pending;          // off the vessel: the tail of the run the handed point starts (unset: run longer than MaxRunScan)
    Point walker;          // a walking bot's position (HandleSpecialMovement): boarding only near the dock; unset: no gate
    // the ride
    Ride ride;             // boarded by the fix; cleared at the exit and by the lost-ride recovery
    std::uint32_t tryEntry = 0;
    std::uint32_t tryMs = 0;
    StopCheck stop;        // the shared stop test answer (board gate, exit gate, deck probe)
};

// One boarding try per bot and ride per BoardRetryMs (wrap-safe); another ride may try at once.
inline bool BoardTryDue(BotState& s, std::uint32_t entry, std::uint32_t nowMs)
{
    if (s.tryMs && s.tryEntry == entry && transport_edge::Since(nowMs, s.tryMs) < BoardRetryMs)
        return false;
    s.tryEntry = entry;
    s.tryMs = nowMs ? nowMs : 1u;
    return true;
}

// ---- per bot: the dock wait the probe ran in ----
struct Wait
{
    bool set = false;
    std::uint32_t ride = 0;
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t startMs = 0;
    std::uint32_t lastMs = 0;
};

// The same wait goes on while the bot is seen at the same ride and dock: S1's episode gap and same-point radius.
inline bool SameWait(Wait const& w, std::uint32_t ride, std::uint32_t map, float x, float y, std::uint32_t nowMs)
{
    if (!w.set || w.ride != ride || w.map != map || transport_edge::Since(nowMs, w.lastMs) > transport_edge::GapMs)
        return false;
    float const dx = w.x - x, dy = w.y - y;
    return dx * dx + dy * dy <= transport_edge::SamePointYards * transport_edge::SamePointYards;
}

// True when a new wait starts.
inline bool NoteWait(Wait& w, std::uint32_t ride, std::uint32_t map, float x, float y, std::uint32_t nowMs)
{
    bool const fresh = !SameWait(w, ride, map, x, y, nowMs);
    if (fresh)
    {
        w = Wait();
        w.set = true;
        w.ride = ride;
        w.map = map;
        w.x = x;
        w.y = y;
        w.startMs = nowMs;
    }
    w.lastMs = nowMs;
    return fresh;
}

// In LastMovement (bot thread only): one probe per dock wait.
struct DeckWait
{
    Wait wait;
    bool probed = false;
};

// Every dock wait tick with the probe on keeps the record; true while this wait has not been probed yet.
inline bool DeckProbeOpen(DeckWait& d, std::uint32_t ride, std::uint32_t map, float x, float y, std::uint32_t nowMs)
{
    if (NoteWait(d.wait, ride, map, x, y, nowMs))
        d.probed = false;
    return !d.probed;
}

// ---- the deck probe (counters only) ----
// Fixed points around the vessel's floor (world axes, no RNG); short ray +-1 yd, long ray DeckProbeUp above to
// DeckProbeDown below the floor.
constexpr std::uint32_t DeckProbeTries = 8;
constexpr float DeckProbeUp = 15.0f;
constexpr float DeckProbeDown = 30.0f;
constexpr float DeckProbeOffsets[DeckProbeTries][2] = {{0, 0}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}, {6, 6}, {-6, -6}, {6, -6}};

struct DeckProbe
{
    std::uint32_t tries = 0, staticShort = 0, deckShort = 0, deckLong = 0, dynLong = 0;
    bool model = false, inTree = false, longDzSet = false;
    float longDz = 0.0f;   // first long deck hit z - floor z (the floor offset error, H3); nearest hit (StopAtFirstHit false)
};

// ---- lines and counters: own budget, Roll -> Account -> Take (count before the cap), minute roll by CAS (S1 pattern) ----
constexpr std::uint32_t LinesPerMinute = 30;

struct Snapshot
{
    std::uint32_t probes = 0, dropped = 0;
    std::uint32_t recoverNear = 0, recoverSearch = 0, recoverNoRoute = 0;   // S3b-1: lost-ride recoveries by kind
    std::uint64_t minute = 0, minutes = 0;
};

struct Counters
{
    std::atomic<std::uint32_t> probes{0}, dropped{0}, logged{0};
    std::atomic<std::uint32_t> recoverNear{0}, recoverSearch{0}, recoverNoRoute{0};
    std::atomic<std::uint64_t> minute{0};

    // Runs only when a [TransportDeck] or [TransportBoard] state= line is about to be written: no line in a minute = no
    // summary for it (mins= spans the gap). One caller per new minute gets the previous window (none for the very first).
    bool Roll(std::uint64_t nowSeconds, Snapshot& out)
    {
        std::uint64_t const now = nowSeconds / 60;
        std::uint64_t seen = minute.load(std::memory_order_relaxed);
        if (seen == now || !minute.compare_exchange_strong(seen, now, std::memory_order_relaxed))
            return false;
        out.probes = probes.exchange(0, std::memory_order_relaxed);
        out.dropped = dropped.exchange(0, std::memory_order_relaxed);
        out.recoverNear = recoverNear.exchange(0, std::memory_order_relaxed);
        out.recoverSearch = recoverSearch.exchange(0, std::memory_order_relaxed);
        out.recoverNoRoute = recoverNoRoute.exchange(0, std::memory_order_relaxed);
        out.minute = seen;
        out.minutes = now - seen;
        logged.exchange(0, std::memory_order_relaxed);
        return seen != 0;
    }

    // After Roll (like S1's Account): the probe lands in its own minute's window, the very first one included.
    void Account() { probes.fetch_add(1, std::memory_order_relaxed); }

    // S3b-1: a lost-ride recovery, after Roll like Account.
    void AccountRecovery(Recovery r)
    {
        std::atomic<std::uint32_t>& counter = r == Recovery::Near ? recoverNear : r == Recovery::Search ? recoverSearch : recoverNoRoute;
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    // Never gives the increment back (a give-back racing Roll's reset would wrap the budget).
    bool Take()
    {
        if (logged.fetch_add(1, std::memory_order_relaxed) < LinesPerMinute)
            return true;
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
};

inline Counters& SharedCounters() { static Counters counters; return counters; }

// ---- line formats ----
inline std::string FormatDeck(std::uint32_t guid, std::uint32_t ride, std::uint32_t displayId, float floorOffset, DeckProbe const& p)
{
    char buf[320];
    std::snprintf(buf, sizeof(buf),
        "[TransportDeck] bot=%u ride=%u display=%u floor_off=%.2f tries=%u static=%u deck_short=%u deck_long=%u dyn_long=%u "
        "mdl=%u in_tree=%u long_dz=%.2f",
        guid, ride, displayId, double(floorOffset), p.tries, p.staticShort, p.deckShort, p.deckLong, p.dynLong,
        p.model ? 1u : 0u, p.inTree ? 1u : 0u, double(p.longDzSet ? p.longDz : -999.0f));
    return buf;
}

// S3b-1: recovery = AiPlayerbot.Path.TransportBoardFix; with it the recovery counters follow dropped=, without it the S3b-0
// line stays byte-identical.
inline std::string FormatSummary(Snapshot const& s, bool recovery = false)
{
    char buf[224];
    if (recovery)
    {
        std::snprintf(buf, sizeof(buf),
            "[TransportBoard] summary probes=%u dropped=%u stranded_near=%u stranded_search=%u stranded_noroute=%u min=%llu mins=%llu",
            s.probes, s.dropped, s.recoverNear, s.recoverSearch, s.recoverNoRoute, static_cast<unsigned long long>(s.minute),
            static_cast<unsigned long long>(s.minutes));
        return buf;
    }
    std::snprintf(buf, sizeof(buf),
        "[TransportBoard] summary probes=%u dropped=%u min=%llu mins=%llu",
        s.probes, s.dropped, static_cast<unsigned long long>(s.minute), static_cast<unsigned long long>(s.minutes));
    return buf;
}

// S3b-1 (OB-10 #1): one line per lost-ride recovery. at_* = the bot when the loss was seen; d_arrival = 2D distance to the
// stored arrival dock, -1 on another map.
inline std::string FormatRecovery(std::uint32_t guid, Ride const& ride, Recovery kind, std::uint32_t atMap, float atX, float atY)
{
    float dArrival = -1.0f;
    if (ride.arrival.set && ride.arrival.map == atMap)
    {
        float const dx = ride.arrival.x - atX, dy = ride.arrival.y - atY;
        dArrival = std::sqrt(dx * dx + dy * dy);
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "[TransportBoard] state=stranded bot=%u ride=%u kind=%s at_map=%u at_x=%.1f at_y=%.1f arrival_map=%u arrival_x=%.1f "
        "arrival_y=%.1f d_arrival=%.1f",
        guid, ride.entry, RecoveryName(kind), atMap, double(atX), double(atY), ride.arrival.map, double(ride.arrival.x),
        double(ride.arrival.y), double(dArrival));
    return buf;
}
}
