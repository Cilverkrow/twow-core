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

// Package "start-zone rule complete" (OB-00 08.11.2026, AiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Complete; design
// ws-10/cli-quest-organic/r1/SZM-V2-RG-CAUSE-AND-GROUP-GATE-DESIGN-20261108.md). The SZM repeat (pin 2l) was INVALID: a held bot
// (B, L3) followed a bot leader of the other cohort out of its start zone (FollowAction, a far move with origin=other). The gate
// covered only the bot's own target choice, the band step and the rescue. V2 1.151: after the release at StartZoneMinLevel the bot
// searched its used-up start zone (empty offers +31 % at L5-7) until a rescue or the second empty offer stepped it out.
//   G1: a held bot does not follow a bot leader outside its start zone.
//   G2: a group copy outside the start zone was already deferred by the gate (SetBestTarget); 1 only logs it.
//   G3: after FollowLeaveSeconds of refused follow in a row the bot leaves its group.
//   V2-a: in the release window the first empty offer counts as the band step (the step rule wants one prior failure).
// 0 off (default and rollback), 1 on; any other value acts like 0.
constexpr std::uint32_t FollowLeaveSeconds = 300;    // G3: refused follow in a row before the leave
constexpr std::uint32_t RefusalFreshSeconds = 60;    // G3: the last refusal at most this old ("in a row")
constexpr std::uint32_t GateLineSeconds = 600;       // follow_gate per bot and leader, group_copy_gate per bot
constexpr std::uint32_t ReleasePushLevels = 3;       // V2-a: StartZoneMinLevel .. StartZoneMinLevel + 2 (owner goal 5-7)

inline bool CompleteOn(std::int32_t value)
{
    return value == 1;
}

// G1: a held bot does not follow a leader whose position is outside its start zone (unknown zone: OutsideStartZone fails open).
inline bool FollowDefers(bool held, bool leaderOutsideStart)
{
    return held && leaderOutsideStart;
}

// G3: refused since (start of the refusal run, 0 = none) and the last refusal (unix seconds); differences only.
inline bool LeaveDue(std::uint32_t refusedSince, std::uint32_t lastRefusal, std::uint32_t now, std::uint32_t seconds)
{
    return refusedSince != 0 && lastRefusal != 0 && std::int32_t(now - refusedSince) >= std::int32_t(seconds) &&
        std::int32_t(now - lastRefusal) <= std::int32_t(RefusalFreshSeconds);
}

// G3 (OB-00 (b) after OB-10's review of #948): never leave a group that has a real player in it, leader or not.
inline bool LeaveAllowed(bool realPlayerMember)
{
    return !realPlayerMember;
}

// The refusal run: a refusal keeps the run while the last one is fresh; otherwise it starts a new run now.
inline std::uint32_t NextRefusedSince(std::uint32_t refusedSince, std::uint32_t lastRefusal, std::uint32_t now)
{
    bool const fresh = refusedSince != 0 && lastRefusal != 0 && std::int32_t(now - lastRefusal) <= std::int32_t(RefusalFreshSeconds);
    return fresh ? refusedSince : now;
}

// V2-a: the release window (StartZoneMinLevel 0: never).
inline bool InReleaseWindow(std::uint32_t minLevel, std::uint32_t level)
{
    return minLevel != 0 && level >= minLevel && level < minLevel + ReleasePushLevels;
}

inline bool ReleasePush(bool on, bool applies, std::uint32_t minLevel, std::uint32_t level, bool inStartZone)
{
    return on && applies && inStartZone && InReleaseWindow(minLevel, level);
}

// The prior failures the band step reads: with the push at least one (QuestBandPolicy BandStepDue: emptyOffer && prior >= 1).
inline std::int32_t PushedPriorFailures(bool push, std::int32_t prior)
{
    return push && prior < 1 ? 1 : prior;
}

// A line at most once per window (lineUntil unix seconds, 0 = none yet; differences only).
inline bool LineDue(std::uint32_t lineUntil, std::uint32_t now)
{
    return lineUntil == 0 || std::int32_t(now - lineUntil) >= 0;
}
}
