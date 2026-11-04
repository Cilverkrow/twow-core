#pragma once

#include "StartZonePolicy.h"

#include <cstdint>

// twow-repo#601 owner rule R1 (04.11.2026, CLI-Deep; ws-00/OWNER-IDEAS-20261104-zone-groups.md): a bot leaves its start zone at
// level 5-7, never below 5, and a stale (dead, taken) quest objective is no reason to change zone. Offline basis (cand4G u1/u2,
// evidence ws-10/cli-quest-organic/cand4/R1-OFFLINE.md): with StaleObjectiveFilter on, 90 zone changes followed a stale skip within
// 600 s (548 cross-zone quest routes and 54 band steps after one); in the base config L1-4 bots crossed zones in 2 of 65 far moves.
// Two switches, both default 0:
//   StaleKeepZone (0 off, 1 all, 2 cohort B, 3 cohort A): a quest search that came back empty while it skipped stale objectives
//     neither widens nor counts toward a band step; for StaleKeepZone.Seconds the bot then takes no quest target outside its zone
//     (turn-ins exempt - a hand-in is progress, not a stale objective).
//   StartZoneMinLevel (0 off, 2..60; owner value 5): below it a roster bot on its own takes no travel target outside its start zone,
//     no band step, and no rescue out of it (outside it, the rescue takes it home).
// Pure functions, no core types.
namespace ai::zone_keep
{
// The bot's start zone: the custom start zones with CustomRaceStartZones (goblin Blackstone Island; high elf Thalassian Highlands
// and Alah'Thalas), otherwise the zone of its race's playercreateinfo row. 0 = unknown (fail open: not gated, logged once).
inline bool StartZoneKnown(std::uint32_t race, bool customStarts, std::uint32_t createZone)
{
    return (customStarts && start_zone::IsCustomStartRace(race)) || createZone != 0;
}

inline bool InStartZone(std::uint32_t race, bool customStarts, std::uint32_t createZone, std::uint32_t zoneId)
{
    if (customStarts && race == start_zone::GoblinRace)
        return zoneId == start_zone::BlackstoneZone;
    if (customStarts && race == start_zone::HighElfRace)
        return zoneId == start_zone::ThalassianZone || zoneId == start_zone::AlahThalasZone;
    return createZone != 0 && zoneId == createZone;
}

// StartZoneMinLevel 0 gates nothing; otherwise every level below it.
inline bool StartZoneGated(std::uint32_t minLevel, std::uint32_t level)
{
    return minLevel != 0 && level < minLevel;
}

// A candidate point of a gated bot outside its start zone is deferred; a point whose zone is unknown (0) is not.
inline bool StartZoneDefers(bool gated, bool startKnown, bool targetInStart, std::uint32_t targetZone)
{
    return gated && startKnown && targetZone != 0 && !targetInStart;
}

// StaleKeepZone: an empty quest search keeps the zone when the switch applies to the bot and the search skipped stale objectives.
inline bool KeepsZone(bool applies, std::uint32_t staleSkipped)
{
    return applies && staleSkipped != 0;
}

// The keep window (unix seconds, 0 = none); differences only.
inline bool KeepActive(std::uint32_t untilSec, std::uint32_t nowSec)
{
    return untilSec != 0 && std::int32_t(untilSec - nowSec) > 0;
}

// Inside the window a quest candidate in another zone is deferred - not a turn-in, and not when either zone is unknown (0).
inline bool KeepZoneDefers(bool active, bool turnIn, std::uint32_t botZone, std::uint32_t targetZone)
{
    return active && !turnIn && botZone != 0 && targetZone != 0 && targetZone != botZone;
}
}
