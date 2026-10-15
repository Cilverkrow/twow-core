#pragma once

#include "PathFailPolicy.h"

#include <cstdint>

// twow-repo#575 (PATH-01 phase 2, wave 0, AiPlayerbot.CustomRaceStartZones, default 0): goblins (race 9) start on
// Blackstone Island (zone 5536, map 1), high elves (race 10) in the Thalassian Highlands (5225) and Alah'Thalas (2040,
// map 0). Off: bots keep the proxy starts (Durotar / Northshire) and every bot avoids these zones, as before. On: the
// real start zones are quest zones for their own race and faction, with the goblin flying machine and the high-elf
// ship as the exits. Pure functions, no core types; the boxes are the PATH-01 #4 zone boxes (PathFailPolicy.h).
namespace ai::start_zone
{
constexpr std::uint32_t GoblinRace = 9;
constexpr std::uint32_t HighElfRace = 10;
constexpr std::uint32_t BlackstoneZone = 5536;
constexpr std::uint32_t ThalassianZone = 5225;
constexpr std::uint32_t AlahThalasZone = 2040;
constexpr std::uint32_t StartAreaMaxLevel = 10;   // same value as quest_search::StartAreaMaxLevel
constexpr std::uint32_t ExemptMaxLevel = 12;      // the high-elf spine ends at L11 (41259)

inline bool IsCustomStartRace(std::uint32_t race)
{
    return race == GoblinRace || race == HighElfRace;
}

// E4: the faction a point of a custom start zone belongs to (None = no start zone).
enum class StartTeam : std::uint8_t
{
    None = 0,
    Horde,
    Alliance,
};

inline StartTeam TeamOfPoint(std::uint32_t mapId, float x, float y)
{
    if (mapId == 1 && path_fail::InAny(path_fail::BlackstoneBoxes, x, y))
        return StartTeam::Horde;
    if (mapId == 0 && path_fail::InAny(path_fail::ThalassianBoxes, x, y))
        return StartTeam::Alliance;
    return StartTeam::None;
}

// E4: a destination (one entry, all its spawns) becomes a start-zone destination only when every point lies in the
// same start zone. An entry with spawns inside and outside keeps its outside points only (the old rule), so a shared
// creature entry never becomes faction-bound.
struct TeamFold
{
    bool any = false;
    bool mixed = false;
    StartTeam team = StartTeam::None;

    void Add(StartTeam point)
    {
        if (!any)
            team = point;
        else if (point != team)
            mixed = true;
        any = true;
    }

    StartTeam Uniform() const
    {
        return any && !mixed ? team : StartTeam::None;
    }
};

// E4 at boot: keep a point of a start zone? Off: never (the old exclusion). On: only for a uniform start-zone entry.
inline bool KeepStartZonePoint(bool on, StartTeam point, StartTeam uniform)
{
    return point == StartTeam::None || (on && uniform != StartTeam::None);
}

// E5: a start-zone destination is possible only for its own faction.
inline bool DestinationAllows(StartTeam tag, bool alliance)
{
    return tag == StartTeam::None || (tag == StartTeam::Alliance) == alliance;
}

inline bool InOwnStartZone(std::uint32_t race, std::uint32_t mapId, float x, float y)
{
    StartTeam const team = TeamOfPoint(mapId, x, y);
    return (race == GoblinRace && team == StartTeam::Horde) || (race == HighElfRace && team == StartTeam::Alliance);
}

// Goblin flying machines (hotfix 8.29): 50597 on the island (taxi path 311 out), 50598 in Sparkwater Port (322 back).
constexpr std::uint32_t IslandPath = 311;
constexpr std::uint32_t ReturnPath = 322;
constexpr float PortMachineX = 819.0f, PortMachineY = -5006.0f, PortMachineZ = 20.0f;
constexpr float PortRadius = 300.0f;        // Sparkwater Port: Nert Blastentom (878, -5069) is 87 yd from 50598
constexpr float ReturnRadius = 1500.0f;     // a goblin anywhere in Durotar walks back to the machine
constexpr std::uint32_t QuestBadNews = 41169;      // island (Taxxlo Rustgate) -> Sparkwater (Nert Blastentom)
constexpr std::uint32_t QuestSolutions = 41170;    // Sparkwater (Nert) -> island (Taxxlo)

// E5 / (0.2) of the plan: the danger check (`TargetZoneLevel`) and the "zone below the bot" search look at area levels.
// Within its own start zone - for the goblin also Sparkwater Port, where 41169 / 41170 change hands - a bot up to
// ExemptMaxLevel ignores both, so the chain is never deferred by an area level (4021 = 36, sub-areas of 5536).
inline bool ZoneLevelExempt(bool on, std::uint32_t race, std::uint32_t level, std::uint32_t mapId, float x, float y)
{
    if (!on || level > ExemptMaxLevel)
        return false;
    if (InOwnStartZone(race, mapId, x, y))
        return true;
    if (race != GoblinRace || mapId != 1)
        return false;
    float const dx = x - PortMachineX, dy = y - PortMachineY;
    return dx * dx + dy * dy <= PortRadius * PortRadius;
}

// E1: off, the factory moves new goblins / high elves to the proxy starts; on, they keep the core start position.
inline bool FactoryUsesProxyStart(std::uint32_t race, bool on)
{
    return !on && IsCustomStartRace(race);
}

// E2: off, a goblin / high elf always repops at its homebind (the proxy start); on, at its real start up to level 10
// and at its homebind above, so a level 25 goblin is not sent back to the island.
inline bool RepopUsesHomebind(std::uint32_t race, std::uint32_t level, bool on)
{
    return IsCustomStartRace(race) && (!on || level > StartAreaMaxLevel);
}

// E3: random teleports. Off: never into 5536 / 5225 / 2040. On: only the zone's own race.
inline bool TeleportSkipsZone(std::uint32_t zoneId, std::uint32_t race, bool on)
{
    bool const blackstone = zoneId == BlackstoneZone;
    bool const thalassian = zoneId == ThalassianZone || zoneId == AlahThalasZone;
    if (!blackstone && !thalassian)
        return false;
    if (!on)
        return true;
    return blackstone ? race != GoblinRace : race != HighElfRace;
}

// E7: the flying machines are taxi paths without a flight master (npc_flags 1, gossip script).
inline bool IsScriptedTaxi(std::uint32_t pathId)
{
    return pathId == IslandPath || pathId == ReturnPath;
}

// E8: the Alah'Thalas flight 303 / 304 lands in Light's Hope (Eastern Plaguelands, L53-60).
constexpr std::uint32_t LightsHopeLevel = 50;
constexpr std::uint32_t LeaveOnFootLevel = 20;

inline bool FlightGated(bool on, std::uint32_t pathObject, std::uint32_t level)
{
    return on && (pathObject == 303 || pathObject == 304) && level < LightsHopeLevel;
}

// E8: below level 20 no walk link leads from the Thalassian Highlands / Alah'Thalas into the Plaguelands.
inline bool WalkOutGated(bool on, std::uint32_t level, std::uint32_t fromMap, float fromX, float fromY,
    std::uint32_t toMap, float toX, float toY)
{
    return on && level < LeaveOnFootLevel && TeamOfPoint(fromMap, fromX, fromY) == StartTeam::Alliance &&
        TeamOfPoint(toMap, toX, toY) != StartTeam::Alliance;
}

// Q1: the goblin chain is a round trip. With 41169 in the log a goblin on the island takes the machine to Sparkwater;
// with 41170 in the log (level <= 10) a goblin in Durotar walks back to 50598 and flies home. 0 = no quest flight.
inline std::uint32_t QuestFlightPath(bool on, std::uint32_t race, std::uint32_t level, bool onIsland, float distToPort,
    bool carriesBadNews, bool carriesSolutions)
{
    if (!on || race != GoblinRace)
        return 0;
    if (onIsland)
        return carriesBadNews ? IslandPath : 0;
    if (carriesSolutions && level <= StartAreaMaxLevel && distToPort <= ReturnRadius)
        return ReturnPath;
    return 0;
}

// Q1: where the bot boards the machine of a quest flight (311 on the island, 322 in Sparkwater Port).
constexpr float IslandMachineX = -571.0f, IslandMachineY = -7850.0f, IslandMachineZ = 52.0f;
constexpr float MachineBoardDistance = 8.0f;   // as hotfix 8.29

struct Point
{
    float x, y, z;
};

inline Point MachineOf(std::uint32_t path)
{
    return path == IslandPath ? Point{ IslandMachineX, IslandMachineY, IslandMachineZ } : Point{ PortMachineX, PortMachineY, PortMachineZ };
}

// 8.29 leaves the island only for persistent roster members; on, every bot on its own may leave.
inline bool IslandLeaveAllowed(bool rosterMember, bool on)
{
    return rosterMember || on;
}

// S1: the high-elf ship (Windrunner, transport 176250, path 323) has no link in the travel graph yet. On, a high elf
// in its start zone with 41259 "Journey to Auberdine" in the log, or from level 12, or from level 10 after 15 minutes
// without progress, walks to the Alah'Thalas pier and is moved to the Auberdine pier ([Travel] state=ship_scripted).
constexpr std::uint32_t QuestJourneyToAuberdine = 41259;
// Ground points (spawn z, OB-50 14.10.): a Sentinel on Windrunner Pier, 13 yd from the ship's stop (z 0 = sea level),
// and Auberdine Sentinel 6086 on the Auberdine pier, 47 yd from the stop.
constexpr float AlahDockX = 4856.3f, AlahDockY = -2961.1f, AlahDockZ = 4.7f;
constexpr std::uint32_t AuberdineMap = 1;
constexpr float AuberdineDockX = 6532.7f, AuberdineDockY = 894.7f, AuberdineDockZ = 6.7f;
constexpr float DockBoardDistance = 10.0f;
constexpr std::uint32_t ShipLevel = ExemptMaxLevel;
constexpr std::uint32_t ShipIdleLevel = StartAreaMaxLevel;
constexpr std::uint32_t ShipIdleSeconds = 15 * 60;

inline bool ShipCandidate(bool on, std::uint32_t race, std::uint32_t mapId, float x, float y)
{
    return on && race == HighElfRace && TeamOfPoint(mapId, x, y) == StartTeam::Alliance;
}

inline bool ShouldTakeShip(std::uint32_t level, std::uint32_t idleSeconds, bool carriesJourney)
{
    return carriesJourney || level >= ShipLevel || (level >= ShipIdleLevel && idleSeconds >= ShipIdleSeconds);
}
}
