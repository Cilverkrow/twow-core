#pragma once

#include "TransportEdgePolicy.h"

#include <atomic>
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
    std::uint64_t minute = 0, minutes = 0;
};

struct Counters
{
    std::atomic<std::uint32_t> probes{0}, dropped{0}, logged{0};
    std::atomic<std::uint64_t> minute{0};

    // Runs only when a [TransportDeck] line is about to be written: no line in a minute = no summary for it (mins= spans
    // the gap). One caller per new minute gets the previous window (none for the very first).
    bool Roll(std::uint64_t nowSeconds, Snapshot& out)
    {
        std::uint64_t const now = nowSeconds / 60;
        std::uint64_t seen = minute.load(std::memory_order_relaxed);
        if (seen == now || !minute.compare_exchange_strong(seen, now, std::memory_order_relaxed))
            return false;
        out.probes = probes.exchange(0, std::memory_order_relaxed);
        out.dropped = dropped.exchange(0, std::memory_order_relaxed);
        out.minute = seen;
        out.minutes = now - seen;
        logged.exchange(0, std::memory_order_relaxed);
        return seen != 0;
    }

    // After Roll (like S1's Account): the probe lands in its own minute's window, the very first one included.
    void Account() { probes.fetch_add(1, std::memory_order_relaxed); }

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

inline std::string FormatSummary(Snapshot const& s)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "[TransportBoard] summary probes=%u dropped=%u min=%llu mins=%llu",
        s.probes, s.dropped, static_cast<unsigned long long>(s.minute), static_cast<unsigned long long>(s.minutes));
    return buf;
}
}
