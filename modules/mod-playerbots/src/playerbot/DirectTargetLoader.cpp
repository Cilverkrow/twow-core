#include "playerbot/playerbot.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/DirectTargetDestinations.h"
#include "playerbot/DirectTargetSnap.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/MemStoresPolicy.h"
#include "playerbot/ServerFacade.h"
#include "Maps/MoveMap.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace ai;
using namespace ai::direct_targets;

// twow-repo#594 / P3-b (design revision 2): the direct-target destinations, built once at boot from what is in memory
// before the bot loader runs - GO spawns (guidpMap), GO templates, area triggers and their teleports, the fishing base
// skills - with a runtime Detour snap (DirectTargetSnap). No DB access, no static file. Every candidate point is either
// kept or logged ([DirectTarget] state=drop_offmesh / drop_rule); one loaded line per class and one loaded_total line.

namespace
{
uint64 ReadRssKb()
{
    uint64 rssKb = 0;
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::stringstream text;
    text << status.rdbuf();
    rssKb = ai::mem_stores::ParseVmRssKb(text.str());
#endif
    return rssKb;
}

struct Counts
{
    uint32 candidates = 0, ok = 0, far = 0, dropOffmesh = 0, dropRule = 0, dedup = 0, points = 0, openEdge = 0, tiles = 0;
    uint32 dist[4] = { 0, 0, 0, 0 };
    std::set<int32> destinations;
};

struct Point3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline float Dist2D(Point3 const& a, Point3 const& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

inline float Dist3D(Point3 const& a, Point3 const& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

inline Point3 HitPoint(SnapHit const& hit)
{
    return Point3{ hit.x, hit.y, hit.z };
}

char const* OffmeshReason(SnapOutcome const& outcome)
{
    return outcome.noMesh ? "nomesh" : (outcome.anyPoly ? "island" : "none");
}

void LogDropOffmesh(Kind kind, uint32 mapId, Point3 const& at, uint32 id, char const* reason)
{
    sLog.outBasic("[DirectTarget] state=drop_offmesh class=%s map=%u x=%.1f y=%.1f z=%.1f id=%u reason=%s",
        KindName(kind), mapId, at.x, at.y, at.z, id, reason);
}

void LogDropRule(Kind kind, uint32 id, char const* reason)
{
    sLog.outBasic("[DirectTarget] state=drop_rule class=%s id=%u reason=%s", KindName(kind), id, reason);
}

void LogLoaded(Kind kind, Counts const& c)
{
    sLog.outBasic("[DirectTarget] state=loaded class=%s candidates=%u ok=%u far=%u drop_offmesh=%u drop_rule=%u dedup=%u "
        "destinations=%u points=%u open_edge=%u dist5=%u dist8=%u dist10=%u dist12p=%u tiles_added=%u", KindName(kind), c.candidates,
        c.ok, c.far, c.dropOffmesh, c.dropRule, c.dedup, uint32(c.destinations.size()), c.points, c.openEdge, c.dist[0], c.dist[1],
        c.dist[2], c.dist[3], c.tiles);
}

// Charges the open edges and the mmap tiles one candidate added to its class: Station and Cooking (and Dungeon and Raid)
// share one loop, so one count around the loop gave both classes the sum. The tiles are never unloaded with
// MMapTileUnload = 0: tiles_added per class is the boot memory cost to measure before enabling classes together.
class Charge
{
public:
    Charge(Counts& counts, std::map<uint32, DirectTargetSnap> const& snaps, MMAP::MMapManager const* mmap)
        : counts(counts), snaps(snaps), mmap(mmap), open(OpenEdges()), tiles(mmap->getLoadedTilesCount()) {}
    ~Charge()
    {
        counts.openEdge += OpenEdges() - open;
        counts.tiles += mmap->getLoadedTilesCount() - tiles;
    }
private:
    uint32 OpenEdges() const
    {
        uint32 total = 0;
        for (auto const& [mapId, snap] : snaps)
            total += snap.OpenEdges();
        return total;
    }
    Counts& counts;
    std::map<uint32, DirectTargetSnap> const& snaps;
    MMAP::MMapManager const* mmap;
    uint32 const open;
    uint32 const tiles;
};

// The GO entries of one type on maps 0 / 1, in entry order with their spawns in guid order: two boots give the same
// dedup choice.
std::vector<std::pair<GameObjectInfo const*, std::vector<AsyncGuidPosition>>> SpawnsOfType(EntryGuidps const& guidpMap, uint32 type,
    bool continentsOnly = true)
{
    std::map<uint32, std::pair<GameObjectInfo const*, std::vector<AsyncGuidPosition>>> sorted;
    for (auto const& [entry, points] : guidpMap)
    {
        if (entry >= 0)
            continue;
        GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(uint32(-entry));
        if (!info || info->type != type)
            continue;
        auto& slot = sorted[uint32(-entry)];
        slot.first = info;
        for (AsyncGuidPosition const& point : points)
            if (!continentsOnly || point.getMapId() == 0 || point.getMapId() == 1)
                slot.second.push_back(point);
        std::sort(slot.second.begin(), slot.second.end(),
            [](AsyncGuidPosition const& a, AsyncGuidPosition const& b) { return a.GetCounter() < b.GetCounter(); });
    }

    std::vector<std::pair<GameObjectInfo const*, std::vector<AsyncGuidPosition>>> result;
    for (auto& [entry, slot] : sorted)
        result.push_back(std::move(slot));
    return result;
}

// Creature spawns without NPC flags in 30-yd cells, with their reaction to the human and orc templates (as
// TravelMgr::SetMobAvoidAreaMap). Only hostile ones are kept.
class HostileIndex
{
public:
    void Build(uint32 mapId)
    {
        FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
        FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);
        for (CreatureDataPair const* creaturePair : WorldPosition(mapId, 1, 1).getCreaturesNear())
        {
            CreatureData const& cData = creaturePair->second;
            CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(cData.creature_id[0]);
            if (!cInfo || cInfo->NpcFlags > 0)
                continue;

            FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->Faction);
            bool const hostileAlliance = PlayerbotAI::GetFactionReaction(humanFaction, factionEntry) < REP_NEUTRAL;
            bool const hostileHorde = PlayerbotAI::GetFactionReaction(orcFaction, factionEntry) < REP_NEUTRAL;
            if (!hostileAlliance && !hostileHorde)
                continue;

            cells[Cell(mapId, cData.position.coord_x, cData.position.coord_y)].push_back(
                Spawn{ cData.position.coord_x, cData.position.coord_y, hostileAlliance, hostileHorde });
        }
    }

    // both: a spawn hostile to both teams; alliance / horde: one hostile to that team.
    void Query(uint32 mapId, float x, float y, bool& both, bool& alliance, bool& horde) const
    {
        both = alliance = horde = false;
        int64 const cx = int64(std::floor(x / HostileRadius)), cy = int64(std::floor(y / HostileRadius));
        for (int64 dx = -1; dx <= 1; ++dx)
            for (int64 dy = -1; dy <= 1; ++dy)
            {
                auto const found = cells.find(Key(mapId, cx + dx, cy + dy));
                if (found == cells.end())
                    continue;
                for (Spawn const& spawn : found->second)
                {
                    if ((spawn.x - x) * (spawn.x - x) + (spawn.y - y) * (spawn.y - y) > HostileRadius * HostileRadius)
                        continue;
                    both = both || (spawn.alliance && spawn.horde);
                    alliance = alliance || spawn.alliance;
                    horde = horde || spawn.horde;
                }
            }
    }
private:
    struct Spawn { float x, y; bool alliance, horde; };
    static uint64 Key(uint32 mapId, int64 cx, int64 cy)
    {
        return (uint64(mapId & 0xFFF) << 52) | (uint64((cx + 0x8000) & 0xFFFFFF) << 24) | uint64((cy + 0x8000) & 0xFFFFFF);
    }
    static uint64 Cell(uint32 mapId, float x, float y)
    {
        return Key(mapId, int64(std::floor(x / HostileRadius)), int64(std::floor(y / HostileRadius)));
    }
    std::unordered_map<uint64, std::vector<Spawn>> cells;
};

AreaTableEntry const* AreaOf(AsyncGuidPosition const& point)
{
    return AreaEntry::GetByAreaFlagAndMap(point.getAreaFlag(), point.getMapId());
}

uint8 HomeZoneTeamMask(AsyncGuidPosition const& point)
{
    uint8 mask = TeamBoth;
    if (point.isEnemyHomeZoneFor(ALLIANCE))
        mask &= uint8(~TeamAlliance);
    if (point.isEnemyHomeZoneFor(HORDE))
        mask &= uint8(~TeamHorde);
    return mask;
}

float Facing(Point3 const& from, Point3 const& to)
{
    float const angle = std::atan2(to.y - from.y, to.x - from.x);
    return angle < 0.0f ? angle + 2.0f * M_PI_F : angle;
}

struct InstanceEntrances
{
    EntranceTravelDestination* destination = nullptr;
    Kind kind = Kind::Dungeon;
    uint32 requiredLevel = 0;
    uint32 requiredPhase = 0;
    bool first = true;
    std::vector<TriggerSpot> triggers;
};
}

void TravelMgr::LoadDirectTargets(EntryGuidps const& guidpMap)
{
    // twow-repo#594 / P3-b: class gates first (all 0: nothing built, nothing logged), then the boot gate.
    uint32 const classes = ConfiguredMask();
    if (!classes)
        return;
    if (!IsBootLoad(InitializeCalls().load(std::memory_order_relaxed)))
    {
        sLog.outBasic("[DirectTarget] state=reload_ignored classes=%u", classes);
        return;
    }

    auto const started = std::chrono::steady_clock::now();
    uint64 const rssBefore = ReadRssKb();
    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    uint32 const tilesBefore = mmap->getLoadedTilesCount();
    uint32 totalPoints = 0, totalDestinations = 0;

    std::map<uint32, DirectTargetSnap> snaps = { { 0, DirectTargetSnap(0) }, { 1, DirectTargetSnap(1) } };
    std::set<uint64> cells;

    // ---- Stations: anvil (1), forge (3), cooking fire (4) -----------------------------------------------------------
    if (classes & (Bit(Kind::Station) | Bit(Kind::Cooking)))
    {
        Counts counts[2];  // Station, Cooking
        std::map<uint32, HostileIndex> hostiles;
        hostiles[0].Build(0);
        hostiles[1].Build(1);

        for (auto const& [info, spawns] : SpawnsOfType(guidpMap, GAMEOBJECT_TYPE_SPELL_FOCUS))
        {
            uint32 const focus = info->spellFocus.focusId;
            if (!IsStationFocus(focus) || !(classes & Bit(KindOfFocus(focus))))
                continue;

            Kind const kind = KindOfFocus(focus);
            Counts& c = counts[kind == Kind::Cooking ? 1 : 0];
            uint32 const dist = info->spellFocus.dist;
            GameObjectInfo const* trapInfo = info->spellFocus.linkedTrapId ? sObjectMgr.GetGameObjectInfo(info->spellFocus.linkedTrapId) : nullptr;
            float const trapRadius = trapInfo && trapInfo->type == GAMEOBJECT_TYPE_TRAP ? float(trapInfo->trap.radius) : 0.0f;

            for (AsyncGuidPosition const& spawn : spawns)
            {
                ++c.candidates;
                Charge const charge(c, snaps, mmap);
                uint32 const mapId = spawn.getMapId();
                DirectTargetSnap& snap = snaps.at(mapId);
                Point3 const go{ spawn.getX(), spawn.getY(), spawn.getZ() };

                snap.LoadTiles(go.x, go.y, SnapFarXY + FocusSearchCap);
                SnapOutcome const outcome = snap.Classify(go.x, go.y, go.z);
                if (outcome.snap == SnapClass::Drop)
                {
                    ++c.dropOffmesh;
                    LogDropOffmesh(kind, mapId, go, spawn.GetCounter(), OffmeshReason(outcome));
                    continue;
                }
                if (outcome.snap == SnapClass::Far)
                {
                    ++c.far;
                    ++c.dropRule;
                    LogDropRule(kind, spawn.GetCounter(), "snap_far");
                    continue;
                }
                ++c.ok;

                Point3 stand = HitPoint(outcome.hit);
                if (!FocusInRange(Dist3D(stand, go), dist))
                {
                    ++c.dropRule;
                    LogDropRule(kind, spawn.GetCounter(), "focus_range");
                    continue;
                }

                // A linked trap (the fire's damage aura): a point outside it, 8 directions at the middle of the band
                // between the trap and the focus range.
                if (!TrapClear(Dist2D(stand, go), trapRadius))
                {
                    Band const band = StationBand(dist, trapRadius);
                    bool found = false;
                    for (uint32 k = 0; band.feasible && k < 8 && !found; ++k)
                    {
                        float const angle = float(k) * M_PI_F / 4.0f;
                        SnapOutcome const probe = snap.Classify(go.x + band.Middle() * std::cos(angle), go.y + band.Middle() * std::sin(angle), go.z);
                        if (probe.snap != SnapClass::Ok)
                            continue;
                        Point3 const candidate = HitPoint(probe.hit);
                        if (FocusInRange(Dist3D(candidate, go), dist) && TrapClear(Dist2D(candidate, go), trapRadius))
                        {
                            stand = candidate;
                            found = true;
                        }
                    }
                    if (!found)
                    {
                        ++c.dropRule;
                        LogDropRule(kind, spawn.GetCounter(), "trap");
                        continue;
                    }
                }

                AreaTableEntry const* area = AreaOf(spawn);
                if (!area || spawn.getAreaLevelOrParent() <= 0)
                {
                    ++c.dropRule;
                    LogDropRule(kind, spawn.GetCounter(), "level");
                    continue;
                }
                // The station key carries the zone below the team slot.
                uint32 const zone = area->ZoneId ? area->ZoneId : area->Id;
                if (!StationZoneFits(zone))
                {
                    ++c.dropRule;
                    LogDropRule(kind, spawn.GetCounter(), "zone_id");
                    continue;
                }

                AsyncGuidPosition point(GuidPosition(0, WorldPosition(mapId, stand.x, stand.y, stand.z, Facing(stand, go))));
                point.setAreaFlag(int16(spawn.getAreaFlag()));

                bool hostileBoth = false, hostileAlliance = false, hostileHorde = false;
                hostiles.at(mapId).Query(mapId, stand.x, stand.y, hostileBoth, hostileAlliance, hostileHorde);
                uint8 const teamMask = TeamMaskAfter(HomeZoneTeamMask(point), hostileAlliance, hostileHorde);
                if (hostileBoth || !teamMask)
                {
                    ++c.dropRule;
                    LogDropRule(kind, spawn.GetCounter(), "hostile");
                    continue;
                }

                if (!cells.insert(CellKey(mapId, focus, stand.x, stand.y)).second)
                {
                    ++c.dedup;
                    continue;
                }

                int32 const key = StationKey(focus, zone, teamMask);
                directPoints.push_back(point);
                StationTravelDestination* destination = AddDestination<StationTravelDestination>(key, TravelDestinationPurpose::Station);
                destination->AddPoint(&directPoints.back());
                c.destinations.insert(key);
                ++c.points;
                ++c.dist[DistBucket(dist)];
            }
        }

        for (Kind kind : { Kind::Station, Kind::Cooking })
        {
            if (!(classes & Bit(kind)))
                continue;
            Counts& c = counts[kind == Kind::Cooking ? 1 : 0];
            LogLoaded(kind, c);
            totalPoints += c.points;
            totalDestinations += uint32(c.destinations.size());
            if (!c.destinations.empty())
                SetBuilt(kind);
        }
    }

    // ---- Fishing: a shore point facing each hole spawn -------------------------------------------------------------
    if (classes & Bit(Kind::Fishing))
    {
        Counts c;
        float minRange = 10.0f, maxRange = 20.0f;
        if (SpellEntry const* fishing = sServerFacade.LookupSpellInfo(7620))
            if (SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(fishing->rangeIndex))
                if (Spells::GetSpellMaxRange(range) > 0.0f)
                {
                    minRange = Spells::GetSpellMinRange(range);
                    maxRange = Spells::GetSpellMaxRange(range);
                }

        std::map<int32, uint8> teamMasks;
        for (auto const& [info, spawns] : SpawnsOfType(guidpMap, GAMEOBJECT_TYPE_FISHINGHOLE))
        {
            Band const band = FishingBand(minRange, maxRange, float(info->fishinghole.radius));
            for (AsyncGuidPosition const& spawn : spawns)
            {
                ++c.candidates;
                Charge const charge(c, snaps, mmap);
                uint32 const mapId = spawn.getMapId();
                Point3 const hole{ spawn.getX(), spawn.getY(), spawn.getZ() };

                // The base skill of the area, else of its zone (GameObject::Use); only 0 is not fishable, the easy coasts
                // are negative.
                AreaTableEntry const* area = AreaOf(spawn);
                int32 reqSkill = area ? sObjectMgr.GetFishingBaseSkillLevel(area->Id) : 0;
                if (!reqSkill && area && area->ZoneId)
                    reqSkill = sObjectMgr.GetFishingBaseSkillLevel(area->ZoneId);
                if (reqSkill == 0)
                {
                    ++c.dropRule;
                    LogDropRule(Kind::Fishing, spawn.GetCounter(), "noskill");
                    continue;
                }

                DirectTargetSnap& snap = snaps.at(mapId);
                snap.LoadTiles(hole.x, hole.y, maxRange + 2.0f);
                if (!snap.HasMesh())
                {
                    ++c.dropOffmesh;
                    LogDropOffmesh(Kind::Fishing, mapId, hole, spawn.GetCounter(), "nomesh");
                    continue;
                }

                // Rings at 14 / 15 / 16 yd x 16 directions on dry ground (no water, steep, magma, slime) of the main
                // component, z within [-1, +8] of the hole.
                bool found = false;
                Point3 shore;
                float shoreDistance = 0.0f, shoreDz = 0.0f;
                for (float ring : ShoreRings)
                {
                    if (!InBand(ring, band))
                        continue;
                    for (uint32 k = 0; k < ShoreDirections; ++k)
                    {
                        float const angle = float(k) * 2.0f * M_PI_F / float(ShoreDirections);
                        bool anyPoly = false;
                        SnapHit const hit = snap.NearestMain(hole.x + ring * std::cos(angle), hole.y + ring * std::sin(angle),
                            hole.z + (ShoreDzMax + ShoreDzMin) / 2.0f, 1.0f, (ShoreDzMax - ShoreDzMin) / 2.0f, ShoreIncludeFlags(),
                            ShoreExcludeFlags(), MainComponentPolys, MainComponentPolys, anyPoly);
                        if (!hit.found)
                            continue;
                        Point3 const candidate = HitPoint(hit);
                        float const distance = Dist2D(candidate, hole), dz = candidate.z - hole.z;
                        if (!InBand(distance, band) || !ShoreDzOk(dz))
                            continue;
                        if (!found || ShoreBetter(distance, dz, shoreDistance, shoreDz))
                        {
                            found = true;
                            shore = candidate;
                            shoreDistance = distance;
                            shoreDz = dz;
                        }
                    }
                }
                if (!found)
                {
                    ++c.dropRule;
                    LogDropRule(Kind::Fishing, spawn.GetCounter(), "noshore");
                    continue;
                }
                ++c.ok;

                AsyncGuidPosition point(GuidPosition(0, WorldPosition(mapId, shore.x, shore.y, shore.z, Facing(shore, hole))));
                point.setAreaFlag(int16(spawn.getAreaFlag()));
                uint8 const pointTeams = HomeZoneTeamMask(point);
                int32 const key = ShoreFishKey(area->Id);
                auto const known = teamMasks.find(key);
                uint8 const teamMask = known == teamMasks.end() ? pointTeams : uint8(known->second & pointTeams);
                if (!teamMask)
                {
                    ++c.dropRule;
                    LogDropRule(Kind::Fishing, spawn.GetCounter(), "hostile");
                    continue;
                }

                // The cell only for a point that passed every rule.
                if (!cells.insert(CellKey(mapId, 9, shore.x, shore.y)).second)
                {
                    ++c.dedup;
                    continue;
                }
                teamMasks[key] = teamMask;

                directPoints.push_back(point);
                ShoreFishTravelDestination* destination = AddDestination<ShoreFishTravelDestination>(key, TravelDestinationPurpose::GatherFishing);
                destination->SetReqSkill(reqSkill);
                destination->SetTeamMask(teamMask);
                destination->AddPoint(&directPoints.back());
                c.destinations.insert(key);
                ++c.points;
            }
        }

        LogLoaded(Kind::Fishing, c);
        totalPoints += c.points;
        totalDestinations += uint32(c.destinations.size());
        if (!c.destinations.empty())
            SetBuilt(Kind::Fishing);
    }

    // ---- Dungeon / raid entrances (area trigger teleports on maps 0 / 1 into an instance) ----------------------------
    if (classes & (Bit(Kind::Dungeon) | Bit(Kind::Raid)))
    {
        Counts counts[2];  // Dungeon, Raid
        uint32 const maxPlayerLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
        std::map<uint32, InstanceEntrances> instances;
        // One point per instance and 30-yd cell: two triggers of one instance collapse, another instance next to it stays.
        std::set<std::pair<uint32, uint64>> entranceCells;

        for (auto const& [triggerId, teleport] : sObjectMgr.GetAllAreaTriggerTeleports())
        {
            AreaTriggerEntry const* trigger = sObjectMgr.GetAreaTrigger(triggerId);
            if (!trigger || (trigger->mapid != 0 && trigger->mapid != 1))
                continue;
            MapEntry const* target = sMapStore.LookupEntry(teleport.destination.mapid);
            if (!target || !target->IsDungeon())
                continue;

            Kind const kind = target->IsRaid() ? Kind::Raid : Kind::Dungeon;
            if (!(classes & Bit(kind)))
                continue;
            Counts& c = counts[kind == Kind::Raid ? 1 : 0];
            ++c.candidates;
            Charge const charge(c, snaps, mmap);

            if (teleport.requiredLevel > maxPlayerLevel)
            {
                ++c.dropRule;
                LogDropRule(kind, triggerId, "level");
                continue;
            }
            if (teleport.requiredCondition != 0)
            {
                ++c.dropRule;
                LogDropRule(kind, triggerId, "gated");
                continue;
            }

            uint32 const mapId = trigger->mapid;
            Point3 const centre{ trigger->x, trigger->y, trigger->z };
            float const extent = trigger->radius > 0.0f ? trigger->radius : std::max(trigger->box_x, trigger->box_y) / 2.0f;
            float const height = trigger->radius > 0.0f ? trigger->radius : trigger->box_z / 2.0f;

            // The 2000-poly rule is the most exposed to tile clipping: the 3x3 tiles around the trigger first.
            DirectTargetSnap& snap = snaps.at(mapId);
            snap.LoadTiles(centre.x, centre.y, extent + EntranceRings[1] + SnapFarXY, true);
            if (!snap.HasMesh())
            {
                ++c.dropOffmesh;
                LogDropOffmesh(kind, mapId, centre, triggerId, "nomesh");
                continue;
            }

            std::vector<float> levels = { centre.z };
            if (height >= 3.0f)
                levels.push_back(centre.z - std::min(height, 10.0f));

            bool found = false, anyPoly = false;
            EntranceProbe best;
            Point3 stand;
            for (float z : levels)
                for (float ring : EntranceRings)
                    for (uint32 k = 0; k < EntranceDirections; ++k)
                    {
                        float const angle = float(k) * 2.0f * M_PI_F / float(EntranceDirections);
                        SnapOutcome const outcome = snap.Classify(centre.x + (ring + extent) * std::cos(angle),
                            centre.y + (ring + extent) * std::sin(angle), z, EntranceComponentPolys);
                        anyPoly = anyPoly || outcome.anyPoly;
                        if (outcome.snap == SnapClass::Drop)
                            continue;

                        Point3 const candidate = HitPoint(outcome.hit);
                        EntranceProbe probe;
                        probe.distance = Dist2D(candidate, centre);
                        if (probe.distance > EntranceLimit(extent))
                            continue;
                        // Never inside the trigger plus the core margin plus radiusMin.
                        probe.outside = !IsPointInAreaTriggerZone(trigger, mapId, candidate.x, candidate.y, candidate.z,
                            EntranceExcludeMargin());
                        if (!probe.outside)
                            continue;
                        probe.large = ComponentLarge(outcome.hit.polys, EntranceComponentPolys, outcome.hit.openEdge);
                        probe.snap = outcome.snap;
                        if (!found || EntranceBetter(probe, best))
                        {
                            found = true;
                            best = probe;
                            stand = candidate;
                        }
                    }

            if (!found)
            {
                ++c.dropOffmesh;
                LogDropOffmesh(kind, mapId, centre, triggerId, anyPoly ? "island" : "none");
                continue;
            }
            if (best.snap == SnapClass::Ok)
                ++c.ok;
            else
                ++c.far;
            if (!best.large)
            {
                ++c.dropRule;
                LogDropRule(kind, triggerId, "small_comp");
                continue;
            }

            uint32 const instanceMap = teleport.destination.mapid;
            AsyncGuidPosition point(GuidPosition(0, WorldPosition(mapId, stand.x, stand.y, stand.z, Facing(stand, centre))));
            point.FetchArea();
            uint8 const teamMask = HomeZoneTeamMask(point);
            if (!teamMask)
            {
                ++c.dropRule;
                LogDropRule(kind, triggerId, "hostile");
                continue;
            }

            // The cell only for a point that passed every rule.
            if (!entranceCells.insert({ instanceMap, CellKey(mapId, kind == Kind::Raid ? 11 : 10, stand.x, stand.y) }).second)
            {
                ++c.dedup;
                continue;
            }

            int32 const key = EntranceKey(instanceMap);
            directPoints.push_back(point);
            EntranceTravelDestination* destination = AddDestination<EntranceTravelDestination>(key,
                kind == Kind::Raid ? TravelDestinationPurpose::RaidEntrance : TravelDestinationPurpose::DungeonEntrance);
            destination->AddPoint(&directPoints.back());

            InstanceEntrances& instance = instances[instanceMap];
            instance.destination = destination;
            instance.kind = kind;
            instance.requiredLevel = instance.first ? teleport.requiredLevel : std::min<uint32>(instance.requiredLevel, teleport.requiredLevel);
            instance.requiredPhase = instance.first ? teleport.requiredPhase : std::min<uint32>(instance.requiredPhase, teleport.requiredPhase);
            destination->SetTeamMask(instance.first ? teamMask : uint8(destination->GetTeamMask() & teamMask));
            instance.first = false;
            instance.triggers.push_back(TriggerSpot{ mapId, centre.x, centre.y });
            c.destinations.insert(key);
            ++c.points;
        }

        // Level ranges from the meeting stones: (a) stone area on the instance map, (b) the map's linked zone,
        // (c) the nearest stone within 600 yd of an entrance trigger; else [required, required + 10].
        std::vector<Stone> stones;
        for (auto const& [info, spawns] : SpawnsOfType(guidpMap, GAMEOBJECT_TYPE_MEETINGSTONE, false))
            for (AsyncGuidPosition const& spawn : spawns)
            {
                AreaEntry const* stoneArea = AreaEntry::GetById(info->meetingstone.areaID);
                stones.push_back(Stone{ info->meetingstone.areaID, stoneArea ? uint32(stoneArea->MapId) : NoMap, spawn.getMapId(),
                    info->meetingstone.minLevel, info->meetingstone.maxLevel, spawn.getX(), spawn.getY() });
            }

        for (auto& [instanceMap, instance] : instances)
        {
            MapEntry const* map = sMapStore.LookupEntry(instanceMap);
            StoneMatch const match = MatchStone(stones, instanceMap, map ? uint32(map->linkedZone) : 0u, instance.triggers);
            uint32 minLevel = 0, maxLevel = 0;
            LevelRange(match, stones, instance.requiredLevel, minLevel, maxLevel);
            instance.destination->SetLevels(minLevel, maxLevel, instance.requiredLevel, maxPlayerLevel);
            instance.destination->SetRequiredPhase(instance.requiredPhase);
            if (instance.kind == Kind::Dungeon)
                NoteDungeonLevels(std::max(minLevel, instance.requiredLevel), maxLevel);
            sLog.outBasic("[DirectTarget] state=level_src map=%u min=%u max=%u src=%s", instanceMap, minLevel, maxLevel,
                LevelSourceName(match.source));
        }

        for (Kind kind : { Kind::Dungeon, Kind::Raid })
        {
            if (!(classes & Bit(kind)))
                continue;
            Counts& c = counts[kind == Kind::Raid ? 1 : 0];
            LogLoaded(kind, c);
            totalPoints += c.points;
            totalDestinations += uint32(c.destinations.size());
            if (!c.destinations.empty())
                SetBuilt(kind);
        }
    }

    // OB-30 (18.10.): boot cost - up to about 130 MB RSS and 60 s are accepted.
    uint32 const tilesAfter = mmap->getLoadedTilesCount();
    uint64 const rssAfter = ReadRssKb();
    uint32 const ms = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    sLog.outBasic("[DirectTarget] state=loaded_total ms=%u points=%u destinations=%u tiles_before=%u tiles_after=%u tiles_added=%u rss_delta_kb=%lld built=%u",
        ms, totalPoints, totalDestinations, tilesBefore, tilesAfter, tilesAfter - tilesBefore,
        (long long)(int64(rssAfter) - int64(rssBefore)), BuiltMask().load(std::memory_order_relaxed));
}
