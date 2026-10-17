#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::shortcut
{
// twow-repo#595 P4 S0 (OB-00 17.10.2026, shortcuts by player proximity): measurement only.
//
// RandomPlayerbotMgr builds a small snapshot of where the real players are, in the world thread right after
// sMapMgr.Update (the map threads are idle there), and publishes it behind a short std::mutex as a
// shared_ptr swap. ShortcutNear(map, x, y, zone) answers from that copy: map threads never read the
// unlocked real-player map for it. In S0 nothing changes behaviour; the [Shortcut] trace counts the bot
// shortcuts (hop, ghost to corpse, repop, far-move arrival) as near / notnear / unknown.
//
// Keys (all default 0 = today's behaviour):
//   AiPlayerbot.Shortcut.NearScope         0 = off (ShortcutNear answers Unknown), 1 = radius, 2 = same zone or radius
//   AiPlayerbot.Shortcut.NearRadius        yards; 0 = map visibility + AiPlayerbot.ReactDistance of the player's map
//   AiPlayerbot.Shortcut.TestWatcherGuids  TEST ONLY: these bot guids count as real players (A/B watcher stand-in)
//   AiPlayerbot.Shortcut.Trace             1 = [Shortcut] counter lines once a minute
//   AiPlayerbot.Shortcut.GroupLoopFix      B1: the legacy random-teleport group loop skips real members

enum class Near : std::uint8_t
{
    NotNear = 0,
    Near = 1,
    Unknown = 2,   // fail closed: the caller keeps today's behaviour
};

constexpr std::uint32_t ScopeOff = 0;
constexpr std::uint32_t ScopeRadius = 1;
constexpr std::uint32_t ScopeZoneOrRadius = 2;

constexpr std::uint64_t StaleMs = 5000;          // an older snapshot answers Unknown
constexpr std::uint64_t BuildIntervalMs = 1000;  // the world thread rebuilds at most once a second
constexpr std::size_t MaxWatchers = 64;          // TestWatcherGuids is capped (bounded build)

// One position a real player (or a test watcher) sees from: the player, or its camera body if that differs.
struct Entry
{
    std::uint32_t map = 0;
    std::uint32_t zone = 0;   // 0 = unknown
    std::uint32_t area = 0;
    float x = 0.0f;
    float y = 0.0f;
    float defaultRadius = 0.0f;   // visibility of the player's map + react distance, taken at build time
    bool watcher = false;
};

struct Snapshot
{
    std::vector<Entry> entries;
    std::uint64_t builtAtMs = 0;   // steady clock, 0 = never built
    std::uint32_t players = 0;
    std::uint32_t watchers = 0;
};

inline std::uint32_t ClampScope(std::int32_t configured)
{
    return configured == 1 || configured == 2 ? std::uint32_t(configured) : ScopeOff;
}

// The [Shortcut] trace measures with the configured scope, or with 2 (the plan's proposal) while NearScope is 0.
inline std::uint32_t MeasureScope(std::uint32_t nearScope)
{
    return nearScope == ScopeRadius ? ScopeRadius : ScopeZoneOrRadius;
}

inline float DefaultRadius(float visibility, float reactDistance)
{
    float const radius = (visibility > 0.0f ? visibility : 0.0f) + (reactDistance > 0.0f ? reactDistance : 0.0f);
    return radius;
}

inline float Radius(float configured, float defaultRadius)
{
    return configured > 0.0f ? configured : defaultRadius;
}

inline bool Fresh(std::uint64_t builtAtMs, std::uint64_t nowMs)
{
    return builtAtMs != 0 && nowMs >= builtAtMs && nowMs - builtAtMs <= StaleMs;
}

inline bool BuildDue(std::uint64_t lastBuildMs, std::uint64_t nowMs)
{
    return lastBuildMs == 0 || nowMs < lastBuildMs || nowMs - lastBuildMs >= BuildIntervalMs;
}

// The near decision. zoneOf() is asked at most once, and only when scope 2 meets an entry on the same map
// outside the radius (a terrain lookup the caller may skip otherwise); it returns 0 when the zone is unknown.
//   snapshot missing / stale, scope off      -> Unknown
//   no entry on the map                      -> NotNear
//   an entry within the radius (2D)          -> Near
//   scope 2 and an entry in the same zone    -> Near
//   scope 2 and a zone 0 on either side      -> Unknown (unless another entry decides Near)
template <class ZoneOf>
Near Evaluate(Snapshot const* snapshot, std::uint64_t nowMs, std::uint32_t scope, float configuredRadius,
    std::uint32_t map, float x, float y, ZoneOf zoneOf)
{
    if (scope != ScopeRadius && scope != ScopeZoneOrRadius)
        return Near::Unknown;
    if (!snapshot || !Fresh(snapshot->builtAtMs, nowMs))
        return Near::Unknown;

    bool onMap = false;
    bool undecided = false;
    bool zoneAsked = false;
    std::uint32_t zone = 0;
    for (Entry const& entry : snapshot->entries)
    {
        if (entry.map != map)
            continue;
        onMap = true;

        float const radius = Radius(configuredRadius, entry.defaultRadius);
        float const dx = entry.x - x;
        float const dy = entry.y - y;
        if (radius > 0.0f && dx * dx + dy * dy <= radius * radius)
            return Near::Near;

        if (scope == ScopeZoneOrRadius)
        {
            if (!zoneAsked)
            {
                zone = zoneOf();
                zoneAsked = true;
            }
            if (zone != 0 && entry.zone != 0)
            {
                if (zone == entry.zone)
                    return Near::Near;
            }
            else
                undecided = true;
        }
    }

    if (!onMap)
        return Near::NotNear;
    return undecided ? Near::Unknown : Near::NotNear;
}

// One teleport, both ends: near if either end is near, else unknown if either is unknown.
inline Near Combine(Near source, Near destination)
{
    if (source == Near::Near || destination == Near::Near)
        return Near::Near;
    if (source == Near::Unknown || destination == Near::Unknown)
        return Near::Unknown;
    return Near::NotNear;
}

// "123, 456,789" -> {123, 456, 789}. Blanks and anything but a plain decimal guid > 0 are skipped,
// duplicates dropped, at most MaxWatchers kept.
inline std::vector<std::uint32_t> ParseGuidList(std::string const& text)
{
    std::vector<std::uint32_t> guids;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        std::size_t end = text.find(',', pos);
        if (end == std::string::npos)
            end = text.size();

        std::size_t first = pos;
        std::size_t last = end;
        while (first < last && (text[first] == ' ' || text[first] == '\t'))
            ++first;
        while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t'))
            --last;

        bool valid = first < last && last - first <= 10;
        std::uint64_t value = 0;
        for (std::size_t i = first; valid && i < last; ++i)
        {
            char const c = text[i];
            if (c < '0' || c > '9')
                valid = false;
            else
                value = value * 10 + std::uint64_t(c - '0');
        }
        if (valid && value > 0 && value <= 0xFFFFFFFFull)
        {
            bool seen = false;
            for (std::uint32_t guid : guids)
                seen = seen || guid == std::uint32_t(value);
            if (!seen && guids.size() < MaxWatchers)
                guids.push_back(std::uint32_t(value));
        }

        pos = end + 1;
    }
    return guids;
}

// The measured shortcuts (critic C02, C06, C08, C01). The teleports themselves are unchanged.
enum class Mech : std::uint8_t
{
    Hop = 0,         // C02 MoveTo2 non-detailed hop
    GhostCorpse,     // C06 ghost to corpse
    Repop,           // C08 repop / death-loop evacuation
    FarMove,         // C01 RosterFarMove arrival
    Count
};

inline char const* MechName(Mech mech)
{
    switch (mech)
    {
        case Mech::Hop: return "hop";
        case Mech::GhostCorpse: return "ghost_corpse";
        case Mech::Repop: return "repop";
        case Mech::FarMove: return "far_move";
        default: return "unknown";
    }
}

// Per-mechanism counters, bumped on map threads (relaxed atomics, no lock), drained once a minute by the world thread.
struct MechCounters
{
    std::atomic<std::uint32_t> near{0};
    std::atomic<std::uint32_t> notNear{0};
    std::atomic<std::uint32_t> unknown{0};
    std::atomic<std::uint32_t> nearSrc{0};
    std::atomic<std::uint32_t> nearDst{0};
};

struct Counters
{
    MechCounters mech[std::size_t(Mech::Count)];
};

inline Counters& SharedCounters()
{
    static Counters counters;
    return counters;
}

inline void Record(Counters& counters, Mech mech, Near source, Near destination)
{
    if (mech >= Mech::Count)
        return;
    MechCounters& c = counters.mech[std::size_t(mech)];
    switch (Combine(source, destination))
    {
        case Near::Near: c.near.fetch_add(1, std::memory_order_relaxed); break;
        case Near::NotNear: c.notNear.fetch_add(1, std::memory_order_relaxed); break;
        default: c.unknown.fetch_add(1, std::memory_order_relaxed); break;
    }
    if (source == Near::Near)
        c.nearSrc.fetch_add(1, std::memory_order_relaxed);
    if (destination == Near::Near)
        c.nearDst.fetch_add(1, std::memory_order_relaxed);
}

// The legacy random-teleport group loop (B1, critic X6). Off: every member but the leader is moved, as
// today. On: a member without its own bot AI or with a real session (IsRealPlayer) is skipped.
inline bool GroupLoopMovesMember(bool fix, bool hasOwnBotAi, bool realSession)
{
    if (!fix)
        return true;
    return hasOwnBotAi && !realSession;
}
}
