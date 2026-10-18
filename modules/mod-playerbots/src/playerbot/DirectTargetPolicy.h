#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ai::direct_targets
{
// twow-repo#594 / P3-b (direct travel targets, design revision 2, 18.10.2026): anvils and forges (Station), cooking
// fires (Cooking), shore points at fishing holes (Fishing) and dungeon / raid entrances, built once at boot from the
// spawns in memory with a Detour snap (DirectTargetLoader.cpp). Pure rules, no game types, no config reads; the
// counters are relaxed atomics. Switches AiPlayerbot.DirectTargets.Station / Cooking / Fishing / Dungeon / Raid, all
// default 0, each behind the master AiPlayerbot.DirectTargets.Enable: with all of them 0 nothing is built, no row is
// added and nothing is logged.

enum class Kind : std::uint8_t
{
    Station = 0,  // anvil (focus 1), forge (focus 3)
    Cooking,      // cooking fire (focus 4)
    Fishing,      // shore point at a fishing hole (GatherFishing)
    Dungeon,      // dungeon entrance
    Raid,         // raid entrance
};
constexpr std::uint32_t KindCount = 5;

inline char const* KindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Station: return "station";
        case Kind::Cooking: return "cooking";
        case Kind::Fishing: return "fishing";
        case Kind::Dungeon: return "dungeon";
        case Kind::Raid: return "raid";
    }
    return "none";
}

inline std::uint32_t Bit(Kind kind)
{
    return 1u << std::uint32_t(kind);
}

// The class switches as a mask; nothing without the master switch.
inline std::uint32_t SwitchMask(bool master, bool station, bool cooking, bool fishing, bool dungeon, bool raid)
{
    if (!master)
        return 0;
    return (station ? Bit(Kind::Station) : 0) | (cooking ? Bit(Kind::Cooking) : 0) | (fishing ? Bit(Kind::Fishing) : 0) |
        (dungeon ? Bit(Kind::Dungeon) : 0) | (raid ? Bit(Kind::Raid) : 0);
}

// ---- Boot only ----------------------------------------------------------------------------------------------------
// A config reload calls PlayerbotAIConfig::Initialize again (PlayerbotMgr / RandomPlayerbotMgr reload); the loader
// builds only on the first call (the boot), on the Master thread before the world thread. One built flag per class:
// a reload can switch a built class off and on again, never activate one that was not built at boot.
inline std::atomic<std::uint32_t>& InitializeCalls()
{
    static std::atomic<std::uint32_t> calls{ 0 };
    return calls;
}

inline std::uint32_t NoteInitialize()
{
    return InitializeCalls().fetch_add(1, std::memory_order_relaxed) + 1;
}

inline bool IsBootLoad(std::uint32_t initializeCalls)
{
    return initializeCalls == 1;
}

inline std::atomic<std::uint32_t>& BuiltMask()
{
    static std::atomic<std::uint32_t> mask{ 0 };
    return mask;
}

inline void SetBuilt(Kind kind)
{
    BuiltMask().fetch_or(Bit(kind), std::memory_order_relaxed);
}

// The cheap gate of every hook: false until a class was built at boot.
inline bool AnyBuilt()
{
    return BuiltMask().load(std::memory_order_relaxed) != 0;
}

// Built at boot and switched on now (switchMask from SwitchMask with the current config).
inline bool Live(std::uint32_t switchMask, Kind kind)
{
    return (BuiltMask().load(std::memory_order_relaxed) & switchMask & Bit(kind)) != 0;
}

// A reload that switches on a class that was not built at boot: the classes to name once ([DirectTarget]
// state=reload_ignored class=). The boot call reports nothing.
inline std::uint32_t NoteReloadSwitches(std::uint32_t switchMask, std::uint32_t initializeCalls)
{
    static std::atomic<std::uint32_t> reported{ 0 };
    if (IsBootLoad(initializeCalls) || !initializeCalls)
        return 0;
    std::uint32_t const ignored = switchMask & ~BuiltMask().load(std::memory_order_relaxed);
    return ignored & ~reported.fetch_or(ignored, std::memory_order_relaxed);
}

// ---- Keys -----------------------------------------------------------------------------------------------------------
// Negative synthetic entries at or below -1e9: GetGameObjectInfo returns nullptr for them (the largest GO entry is
// 7,000,035) and they never meet the positive legacy fish keys. Station: -(1e9 + focus * 1e5 + teamSlot * 2e4 + zone),
// teamSlot 0 = both teams (the design key), 1 = Alliance only, 2 = Horde only (a point near guards of one faction).
// ShoreFish: -(1.1e9 + area). Entrance: -(1.2e9 + instance map).
constexpr std::int32_t StationBase = 1000000000;
constexpr std::int32_t ShoreFishBase = 1100000000;
constexpr std::int32_t EntranceBase = 1200000000;
constexpr std::int32_t FamilyEnd = 1300000000;
constexpr std::uint32_t StationTeamSlotSize = 20000;  // zone ids stay below it (else drop_rule reason=zone_id)

// A zone id the station key can carry without wrapping into the team slot.
inline bool StationZoneFits(std::uint32_t zone)
{
    return zone < StationTeamSlotSize;
}

enum class Family : std::uint8_t
{
    None,
    Station,
    ShoreFish,
    Entrance,
};

constexpr std::uint8_t TeamAlliance = 1;
constexpr std::uint8_t TeamHorde = 2;
constexpr std::uint8_t TeamBoth = TeamAlliance | TeamHorde;

inline std::uint32_t TeamSlot(std::uint8_t teamMask)
{
    return teamMask == TeamAlliance ? 1u : (teamMask == TeamHorde ? 2u : 0u);
}

inline std::int32_t StationKey(std::uint32_t focus, std::uint32_t zone, std::uint8_t teamMask = TeamBoth)
{
    return -(StationBase + std::int32_t(focus) * 100000 + std::int32_t(TeamSlot(teamMask) * StationTeamSlotSize) +
        std::int32_t(zone % StationTeamSlotSize));
}

inline std::int32_t ShoreFishKey(std::uint32_t area)
{
    return -(ShoreFishBase + std::int32_t(area));
}

inline std::int32_t EntranceKey(std::uint32_t mapId)
{
    return -(EntranceBase + std::int32_t(mapId));
}

inline bool IsDirectKey(std::int32_t key)
{
    return key <= -StationBase && key > -FamilyEnd;
}

inline Family FamilyOf(std::int32_t key)
{
    if (!IsDirectKey(key))
        return Family::None;
    std::int32_t const value = -key;
    if (value >= EntranceBase)
        return Family::Entrance;
    if (value >= ShoreFishBase)
        return Family::ShoreFish;
    return Family::Station;
}

inline std::uint32_t StationFocusOf(std::int32_t key)
{
    return FamilyOf(key) == Family::Station ? std::uint32_t((-key - StationBase) / 100000) : 0;
}

inline std::uint32_t StationZoneOf(std::int32_t key)
{
    return FamilyOf(key) == Family::Station ? std::uint32_t((-key - StationBase) % 100000) % StationTeamSlotSize : 0;
}

inline std::uint8_t StationTeamOf(std::int32_t key)
{
    if (FamilyOf(key) != Family::Station)
        return 0;
    std::uint32_t const slot = std::uint32_t((-key - StationBase) % 100000) / StationTeamSlotSize;
    return slot == 1 ? TeamAlliance : (slot == 2 ? TeamHorde : TeamBoth);
}

inline std::uint32_t ShoreFishAreaOf(std::int32_t key)
{
    return FamilyOf(key) == Family::ShoreFish ? std::uint32_t(-key - ShoreFishBase) : 0;
}

inline std::uint32_t EntranceMapOf(std::int32_t key)
{
    return FamilyOf(key) == Family::Entrance ? std::uint32_t(-key - EntranceBase) : 0;
}

inline bool TeamAllowed(std::uint8_t teamMask, bool alliance)
{
    return (teamMask & (alliance ? TeamAlliance : TeamHorde)) != 0;
}

// A creature spawn within HostileRadius hostile to both teams drops the point, one hostile to one team takes that
// team's bit (as an enemy home zone does).
constexpr float HostileRadius = 30.0f;

inline std::uint8_t TeamMaskAfter(std::uint8_t teamMask, bool hostileToAlliance, bool hostileToHorde)
{
    if (hostileToAlliance)
        teamMask &= std::uint8_t(~TeamAlliance);
    if (hostileToHorde)
        teamMask &= std::uint8_t(~TeamHorde);
    return teamMask;
}

// ---- Focus ----------------------------------------------------------------------------------------------------------
constexpr std::uint32_t FocusAnvil = 1;
constexpr std::uint32_t FocusForge = 3;
constexpr std::uint32_t FocusCooking = 4;

inline bool IsStationFocus(std::uint32_t focus)
{
    return focus == FocusAnvil || focus == FocusForge || focus == FocusCooking;
}

inline Kind KindOfFocus(std::uint32_t focus)
{
    return focus == FocusCooking ? Kind::Cooking : Kind::Station;
}

// The focus mask of the station need: anvil 1, forge 2, cooking fire 4.
inline std::uint32_t FocusBit(std::uint32_t focus)
{
    switch (focus)
    {
        case FocusAnvil: return 1;
        case FocusForge: return 2;
        case FocusCooking: return 4;
        default: return 0;
    }
}

// ---- Snap -----------------------------------------------------------------------------------------------------------
// As the offline tool (p3b-data/snap.cpp): the nearest poly (GROUND | WATER) of a link component of at least
// MainComponentPolys; snap_ok within 5 yd 2D and |dz| <= 3, snap_far within 15 / 5, else drop_offmesh (island: a poly
// was in range, but only on a small component; none: no poly in range; nomesh: no navmesh for the map).
constexpr float SnapOkXY = 5.0f;
constexpr float SnapOkDz = 3.0f;
constexpr float SnapFarXY = 15.0f;
constexpr float SnapFarDz = 5.0f;
constexpr std::uint32_t MainComponentPolys = 200;
constexpr std::uint32_t EntranceComponentPolys = 2000;

enum class SnapClass : std::uint8_t
{
    Ok,
    Far,
    Drop,
};

inline SnapClass ClassifySnap(bool okFound, float okXY, float okDz, bool farFound, float farXY, float farDz)
{
    if (okFound && okXY <= SnapOkXY && std::fabs(okDz) <= SnapOkDz)
        return SnapClass::Ok;
    if (farFound && farXY <= SnapFarXY && std::fabs(farDz) <= SnapFarDz)
        return SnapClass::Far;
    return SnapClass::Drop;
}

// A Detour link exists only when both tiles are loaded: an external portal edge without a link counts as "open edge ->
// large" instead of clipping the component at the edge of the loaded set.
inline bool ComponentLarge(std::uint32_t polys, std::uint32_t threshold, bool openEdge)
{
    return openEdge || polys >= threshold;
}

// ---- Station --------------------------------------------------------------------------------------------------------
// MoveToTravelTargetAction offsets the final target by up to radiusMin * [0.5 .. 1] (within 100 yd), so every rule
// below keeps that offset: the unsnapped final step stays inside the focus range and outside the trap.
constexpr float StationRadius = 2.0f;
constexpr float StationMargin = 0.5f;
constexpr float FocusSearchCap = 10.0f;  // Spell::CheckCast searches the focus within 10 yd

// The arrival check of a station / entrance: the final move ends up to radiusMin away from the point, and IsIn measures
// 3D against the snapped z, so radiusMin alone was borderline (no arrival, a new move, a move retry). The margin is the
// one every load rule already keeps (focus range, trap, entrance outside margin).
inline float ArrivalRadius(float radiusMin)
{
    return radiusMin + StationMargin;
}

inline float FocusRange(std::uint32_t dist)
{
    return std::min(float(dist), FocusSearchCap);
}

inline bool FocusInRange(float distance3d, std::uint32_t dist)
{
    return distance3d + StationRadius + StationMargin <= FocusRange(dist);
}

inline bool TrapClear(float distance2d, float trapRadius)
{
    return trapRadius <= 0.0f || distance2d >= trapRadius + StationRadius + StationMargin;
}

// The band between the trap and the focus range; the 8 probe directions sit at its middle.
struct Band
{
    float low = 0.0f;
    float high = 0.0f;
    bool feasible = false;
    float Middle() const { return (low + high) / 2.0f; }
};

inline Band StationBand(std::uint32_t dist, float trapRadius)
{
    Band band;
    band.low = trapRadius > 0.0f ? trapRadius + StationRadius + StationMargin : 0.0f;
    band.high = FocusRange(dist) - StationRadius - StationMargin;
    band.feasible = band.high > 0.0f && band.low <= band.high;
    return band;
}

// Per-dist counts of the loaded line: dist5 (<= 5), dist8 (6..8), dist10 (9..10), dist12p (> 10).
inline std::uint32_t DistBucket(std::uint32_t dist)
{
    return dist <= 5 ? 0 : (dist <= 8 ? 1 : (dist <= 10 ? 2 : 3));
}

constexpr float StationMaxDistance = 1000.0f;  // same map, from the bot
constexpr float EntranceMaxDistance = 1500.0f;
constexpr float DedupCell = 30.0f;

// One point per (map, class, 30-yd cell).
inline std::uint64_t CellKey(std::uint32_t mapId, std::uint32_t slot, float x, float y)
{
    std::int64_t const cx = std::int64_t(std::floor(x / DedupCell)) + 0x8000;
    std::int64_t const cy = std::int64_t(std::floor(y / DedupCell)) + 0x8000;
    return (std::uint64_t(mapId & 0xFFF) << 52) | (std::uint64_t(slot & 0xF) << 48) | (std::uint64_t(cx & 0xFFFFFF) << 24) |
        std::uint64_t(cy & 0xFFFFFF);
}

// ---- Fishing --------------------------------------------------------------------------------------------------------
// Core catch chance: skill - zone skill + 5 (GameObject::Use). The floor skill >= reqSkill + 45 is a chance of at least
// 50 % (OB-10 18.10.: a policy constant, not a switch). Only 0 is "not fishable": the easy coasts have negative base
// skills (-70 / -20: Westfall, Barrens, Darkshore, Silverpine, Loch Modan), which the floor passes at once - the core
// succeeds there with skill >= zone skill as well.
constexpr std::int32_t FishingChanceFloorSkill = 45;

inline std::int32_t FishingChance(std::int32_t skill, std::int32_t zoneSkill)
{
    return std::max<std::int32_t>(0, std::min<std::int32_t>(100, skill - zoneSkill + 5));
}

inline bool FishingSkillOk(std::uint32_t skill, std::int32_t reqSkill, std::uint32_t skillMax)
{
    return skill > 0 && reqSkill != 0 && std::int32_t(skill) >= reqSkill + FishingChanceFloorSkill && skill < skillMax;
}

// The bobber lands 10..20 yd away (spell 7620 range) with a small angle spread: the accepted 2D distance from the hole
// centre is [min + r, max - r], r = the hole radius (about 13.5 - 16.5 yd).
inline Band FishingBand(float minRange, float maxRange, float holeRadius)
{
    Band band;
    band.low = minRange + holeRadius;
    band.high = maxRange - holeRadius;
    band.feasible = band.low <= band.high;
    return band;
}

constexpr float ShoreRings[3] = { 14.0f, 15.0f, 16.0f };
constexpr std::uint32_t ShoreDirections = 16;
constexpr float ShorePreferred = 15.0f;
constexpr float ShoreDzMin = -1.0f;  // snapped z - hole z
constexpr float ShoreDzMax = 8.0f;

inline bool InBand(float distance, Band const& band)
{
    return band.feasible && distance >= band.low && distance <= band.high;
}

inline bool ShoreDzOk(float dz)
{
    return dz >= ShoreDzMin && dz <= ShoreDzMax;
}

// Minimal |d - 15|, then the smaller |dz|.
inline bool ShoreBetter(float distanceA, float dzA, float distanceB, float dzB)
{
    float const a = std::fabs(distanceA - ShorePreferred), b = std::fabs(distanceB - ShorePreferred);
    if (std::fabs(a - b) > 0.01f)
        return a < b;
    return std::fabs(dzA) < std::fabs(dzB);
}

// ---- Entrances ------------------------------------------------------------------------------------------------------
// No point inside the trigger plus the core margin (5 yd, HandleAreaTriggerOpcode) plus radiusMin 3: a stale
// lastAreaTrigger relay could port the bot. Probes on rings at 12 and 18 yd beyond the trigger extent, 8 directions, at
// the trigger z and (h >= 3) at z - min(h, 10).
constexpr float EntranceRadius = 3.0f;
constexpr float TriggerCoreMargin = 5.0f;
constexpr float EntranceRings[2] = { 12.0f, 18.0f };
constexpr std::uint32_t EntranceDirections = 8;

inline float EntranceExcludeMargin()
{
    return TriggerCoreMargin + EntranceRadius;
}

// The snapped point must stay within this 2D distance of the trigger centre (the outer ring included).
inline float EntranceLimit(float extent)
{
    return std::max(20.0f, extent + EntranceRings[1]);
}

struct EntranceProbe
{
    bool large = false;      // component >= 2000 polys (or an open edge)
    bool outside = false;    // outside the trigger + margin
    SnapClass snap = SnapClass::Drop;
    float distance = 0.0f;   // 2D to the trigger centre
};

// Order: component >= 2000 > outside the margin > snap_ok > snap_far > distance.
inline bool EntranceBetter(EntranceProbe const& a, EntranceProbe const& b)
{
    if (a.large != b.large)
        return a.large;
    if (a.outside != b.outside)
        return a.outside;
    if (a.snap != b.snap)
        return a.snap == SnapClass::Ok;
    return a.distance < b.distance;
}

// ---- Level ranges ---------------------------------------------------------------------------------------------------
// Meeting stones (GO type 23) give the dungeon level ranges: (a) the stone's area belongs to the instance map, (b) the
// map's linked zone, (c) the nearest stone within 600 yd of an entrance trigger on the same continent; else
// [requiredLevel, requiredLevel + 10].
enum class LevelSource : std::uint8_t
{
    StoneArea,
    LinkedZone,
    NearStone,
    Required,
};

inline char const* LevelSourceName(LevelSource source)
{
    switch (source)
    {
        case LevelSource::StoneArea: return "stone_area";
        case LevelSource::LinkedZone: return "linked_zone";
        case LevelSource::NearStone: return "near_stone";
        case LevelSource::Required: return "required";
    }
    return "none";
}

constexpr float StoneMatchRadius = 600.0f;
constexpr std::uint32_t FallbackLevelSpan = 10;
constexpr std::uint32_t NoMap = 0xFFFFFFFF;

struct Stone
{
    std::uint32_t areaId = 0;       // the template's area (meetingstone.areaID)
    std::uint32_t areaMapId = NoMap;  // the map of that area (AreaTable), NoMap when unknown
    std::uint32_t mapId = 0;        // the spawn's map
    std::uint32_t minLevel = 0, maxLevel = 0;
    float x = 0.0f, y = 0.0f;
};

struct TriggerSpot
{
    std::uint32_t mapId = 0;
    float x = 0.0f, y = 0.0f;
};

struct StoneMatch
{
    int index = -1;  // into the stones, -1 = none
    LevelSource source = LevelSource::Required;
};

// Stones without a max level never match; in each rule the first stone (spawn order) wins, the near rule takes the
// nearest (the later one on a tie) within 600 yd of any trigger on the same map.
inline StoneMatch MatchStone(std::vector<Stone> const& stones, std::uint32_t instanceMap, std::uint32_t linkedZone,
    std::vector<TriggerSpot> const& triggers)
{
    for (std::size_t i = 0; i < stones.size(); ++i)
        if (stones[i].maxLevel && stones[i].areaMapId == instanceMap)
            return StoneMatch{ int(i), LevelSource::StoneArea };
    if (linkedZone)
        for (std::size_t i = 0; i < stones.size(); ++i)
            if (stones[i].maxLevel && stones[i].areaId == linkedZone)
                return StoneMatch{ int(i), LevelSource::LinkedZone };

    StoneMatch match;
    float nearest = StoneMatchRadius;
    for (std::size_t i = 0; i < stones.size(); ++i)
        for (TriggerSpot const& at : triggers)
        {
            if (stones[i].mapId != at.mapId || !stones[i].maxLevel)
                continue;
            float const distance = std::sqrt((stones[i].x - at.x) * (stones[i].x - at.x) + (stones[i].y - at.y) * (stones[i].y - at.y));
            if (distance <= nearest)
            {
                nearest = distance;
                match = StoneMatch{ int(i), LevelSource::NearStone };
            }
        }
    return match;
}

// The stone's range, else [requiredLevel, requiredLevel + 10].
inline void LevelRange(StoneMatch const& match, std::vector<Stone> const& stones, std::uint32_t requiredLevel,
    std::uint32_t& minLevel, std::uint32_t& maxLevel)
{
    bool const found = match.index >= 0 && std::size_t(match.index) < stones.size();
    minLevel = found ? stones[std::size_t(match.index)].minLevel : requiredLevel;
    maxLevel = found ? stones[std::size_t(match.index)].maxLevel : requiredLevel + FallbackLevelSpan;
}

inline bool DungeonLevelFit(std::uint32_t level, std::uint32_t minLevel, std::uint32_t maxLevel, std::uint32_t requiredLevel)
{
    return std::max(minLevel, requiredLevel) <= level && level <= maxLevel;
}

inline bool RaidLevelFit(std::uint32_t level, std::uint32_t maxPlayerLevel, std::uint32_t requiredLevel, bool phaseReached)
{
    return level == maxPlayerLevel && level >= requiredLevel && phaseReached;
}

// ---- Need and cooldowns ---------------------------------------------------------------------------------------------
constexpr std::uint32_t NeedCacheSeconds = 60;
constexpr std::uint32_t StationCooldownSeconds = 10 * 60;  // shared by Station and Cooking
constexpr std::uint32_t DungeonCooldownSeconds = 30 * 60;
constexpr std::uint32_t RaidCooldownSeconds = 60 * 60;

inline std::uint32_t CooldownSeconds(Kind kind)
{
    switch (kind)
    {
        case Kind::Station:
        case Kind::Cooking: return StationCooldownSeconds;
        case Kind::Dungeon: return DungeonCooldownSeconds;
        case Kind::Raid: return RaidCooldownSeconds;
        default: return 0;  // fishing follows the gather purpose rules
    }
}

inline bool CooldownRunning(std::uint32_t now, std::uint32_t until)
{
    return until && now < until;
}

// Station and Cooking share one cooldown (and one request purpose).
inline bool SharesCooldown(Kind a, Kind b)
{
    auto const stationFamily = [](Kind kind) { return kind == Kind::Station || kind == Kind::Cooking; };
    return a == b || (stationFamily(a) && stationFamily(b));
}

// The class cooldown starts at the pick, but the need is also the trip's travel condition (TravelTarget::CheckStatus
// drops a target whose condition turned false, re-read every 5 s): the bot's own open trip of that class ignores it,
// or every station / entrance trip would end as inactive within seconds of the pick.
inline bool CooldownBlocksNeed(std::uint32_t now, std::uint32_t until, bool ownTripOpen)
{
    return !ownTripOpen && CooldownRunning(now, until);
}

// An empty Station / Dungeon / Raid request (no destination in reach) waits this long before the next one: every
// target expiry clears "no active travel destinations", and each request is an async job (5 workers, ParkCap 32).
constexpr std::uint32_t EmptyCooldownSeconds = 5 * 60;

// ---- Failure memory -------------------------------------------------------------------------------------------------
// Per destination, for every bot: move_retry cooldowns, TRAVEL expiries and station no_focus count as failures; an
// arrival (for a station: the first craft of the trip, so a station the bots reach but cannot craft at fails too) as a
// success. failures >= 3 and failures > 2 * arrivals disables it for DisableSeconds; the counts start again from 0
// afterwards, so a crowded start or fights on the way do not take a good zone-wide destination away for the uptime.
constexpr std::uint32_t DisableMinFailures = 3;
constexpr std::uint32_t DisableSeconds = 60 * 60;

inline bool ShouldDisable(std::uint32_t failures, std::uint32_t arrivals)
{
    return failures >= DisableMinFailures && failures > 2 * arrivals;
}

inline std::uint32_t DisabledUntil(std::uint32_t now)
{
    return now + DisableSeconds;
}

// A TRAVEL expiry while the startup long-move budget held the bot back (twow-repo#540: the move returns without
// moving) is no failure of the destination.
inline bool CountsAsFailure(bool travelExpiry, bool startupBudgetHeld)
{
    return !(travelExpiry && startupBudgetHeld);
}

// ---- Minute summary -------------------------------------------------------------------------------------------------
constexpr std::uint32_t EmptySlots = 3;  // station (Station + Cooking requests), dungeon, raid

struct Counters
{
    std::atomic<std::uint32_t> picks[KindCount] = {};
    std::atomic<std::uint32_t> empty[EmptySlots] = {};
    std::atomic<std::uint32_t> crafts{ 0 };
    std::atomic<std::uint32_t> noFocus{ 0 };
    std::atomic<std::uint32_t> fail{ 0 };
    std::atomic<std::uint32_t> disabled{ 0 };  // disables since boot (not reset)
};

inline Counters& Stats()
{
    static Counters counters;
    return counters;
}

// The fields appended to [DirectTarget] state=summary after the P3-0 reasons; empty when no class was built, so the
// P3-a line stays as it was.
inline std::string FormatSummaryFields(std::uint32_t const picks[KindCount], std::uint32_t const empty[EmptySlots],
    std::uint32_t crafts, std::uint32_t noFocus, std::uint32_t fail, std::uint32_t disabled)
{
    char line[320];
    std::snprintf(line, sizeof(line),
        " picks=station:%u,cooking:%u,fishing:%u,dungeon:%u,raid:%u empty=station:%u,dungeon:%u,raid:%u crafts=%u no_focus=%u fail=%u disabled=%u",
        picks[0], picks[1], picks[2], picks[3], picks[4], empty[0], empty[1], empty[2], crafts, noFocus, fail, disabled);
    return line;
}

inline std::string TakeSummaryFields()
{
    if (!AnyBuilt())
        return std::string();

    Counters& c = Stats();
    std::uint32_t picks[KindCount], empty[EmptySlots];
    for (std::uint32_t i = 0; i < KindCount; ++i)
        picks[i] = c.picks[i].exchange(0, std::memory_order_relaxed);
    for (std::uint32_t i = 0; i < EmptySlots; ++i)
        empty[i] = c.empty[i].exchange(0, std::memory_order_relaxed);
    return FormatSummaryFields(picks, empty, c.crafts.exchange(0, std::memory_order_relaxed),
        c.noFocus.exchange(0, std::memory_order_relaxed), c.fail.exchange(0, std::memory_order_relaxed),
        c.disabled.load(std::memory_order_relaxed));
}

// ---- Trips ----------------------------------------------------------------------------------------------------------
// OB-10 (18.10.): every trip logs a start (state=pick) and an end (state=trip_end result=), where the bot's travel target
// leaves the destination: a failure (move_retry, expired, no_focus) with its reason, otherwise crafted / arrived, and
// for a trip that never arrived how it ended - inactive (cooldown), ended (expired), reset (null target), replaced (another
// destination). Unpaired: a bot that logs out mid-trip (no end line).
inline char const* TripResult(bool arrived, std::uint32_t crafts, char const* otherwise = "inactive")
{
    return crafts ? "crafted" : (arrived ? "arrived" : otherwise);
}
}
