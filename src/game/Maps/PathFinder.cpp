/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "MoveMap.h"
#include "GridMap.h"
#include "Creature.h"
#include "PathFinder.h"
#include "Log.h"
#include "Map.h"
#include "Transport.h"
#include "World.h"
#include "VMapFactory.h"
#include "PathUnitless.h"

#include <array>
#include <chrono>

thread_local uint32 PathFinderStats::builds = 0;
thread_local uint32 PathFinderStats::buildMs = 0;
#include <tuple>

#include "Detour/Include/DetourCommon.h"

// Distance to target
#define SMOOTH_PATH_SLOP 0.4f
// Distance between path steps
#define SMOOTH_PATH_STEP_SIZE 2.0f


using restricted_movement_t = std::tuple<uint32, uint32, uint32>; // map id, area id, zone id

auto NoMovementAreas = make_array<restricted_movement_t>(
    std::make_tuple(1u, 5122u, 5121u), // The Shallow Strand, Tel'Abim
    std::make_tuple(1u, 5128u, 5121u), // Jagged Isles, Tel'Abim
    std::make_tuple(1u, 5129u, 5121u) // The Washing Shore, Tel'Abim
);

////////////////// twow-repo#592 (PATH-02 variant A) //////////////////

// Calls that failed closed without a Unit, by reason. Static storage, so zero before the first PathInfo.
static path_unitless::Counters s_unitlessRejects;

PathTerrainRef::PathTerrainRef(PathTerrainRef const& other) : m_terrain(other.m_terrain)
{
    if (m_terrain)
        m_terrain->AddRef();
}

PathTerrainRef& PathTerrainRef::operator=(PathTerrainRef const& other)
{
    if (this != &other)
    {
        if (other.m_terrain)
            other.m_terrain->AddRef();
        if (m_terrain)
            m_terrain->Release();
        m_terrain = other.m_terrain;
    }
    return *this;
}

PathTerrainRef::~PathTerrainRef()
{
    if (m_terrain)
        m_terrain->Release();
}

void PathTerrainRef::Adopt(TerrainInfo* referenced)
{
    if (m_terrain)
        m_terrain->Release();
    m_terrain = referenced;
}

// The owner accessors. Left of ':' stands the expression this file used before #592, unchanged; the
// right side is only reached without a Unit, i.e. in map mode (the fail-closed modes return earlier).
inline uint32 PathInfo::srcMapId() const { return m_sourceUnit ? m_sourceUnit->GetMapId() : m_params.mapId; }
inline bool PathInfo::srcIgnorePathfinding() const { return m_sourceUnit ? m_sourceUnit->HasUnitState(UNIT_STAT_IGNORE_PATHFINDING) : m_params.ignorePathfinding; }
inline float PathInfo::srcBoundingRadius() const { return m_sourceUnit ? m_sourceUnit->GetObjectBoundingRadius() : m_params.boundingRadius; }
inline bool PathInfo::srcCanSwim() const { return m_sourceUnit ? m_sourceUnit->CanSwim() : m_params.canSwim; }
inline bool PathInfo::srcCanFly() const { return m_sourceUnit ? m_sourceUnit->CanFly() : m_params.canFly; }
inline bool PathInfo::srcCanWalk() const { return m_sourceUnit ? m_sourceUnit->CanWalk() : m_params.canWalk; }
inline bool PathInfo::srcCreatureCanFly() const { return m_sourceUnit ? (m_sourceUnit->GetTypeId() == TYPEID_UNIT && ((Creature*)m_sourceUnit)->CanFly()) : m_params.creatureCanFly; }
inline TerrainInfo const* PathInfo::srcTerrain() const { return m_sourceUnit ? m_sourceUnit->GetTerrain() : m_terrain.get(); }
inline uint32 PathInfo::srcLogGuid() const { return m_sourceUnit ? m_sourceUnit->GetGUIDLow() : m_params.logGuidLow; }
inline char const* PathInfo::srcLogName() const { return m_sourceUnit ? m_sourceUnit->GetName() : "<no unit>"; }

// Collision test of the first case in BuildPolyPath. The Unit keeps Map::FindCollisionModel (its two
// coordinate asserts and Map::GetId). Map mode asks the VMapManager with mapId, the same call that
// Map::FindCollisionModel makes; invalid coordinates count as a collision, so the navmesh decides.
bool PathInfo::srcFindCollision(Vector3 const& start, Vector3 const& end) const
{
    if (m_sourceUnit)
        return m_sourceUnit->GetMap()->FindCollisionModel(start.x, start.y, start.z, end.x, end.y, end.z) != nullptr;

    if (!MaNGOS::IsValidMapCoord(start.x, start.y, start.z) || !MaNGOS::IsValidMapCoord(end.x, end.y, end.z))
        return true;
    return VMAP::VMapFactory::createOrGetVMapManager()->FindCollisionModel(m_params.mapId, start.x, start.y, start.z, end.x, end.y, end.z) != nullptr;
}

// Ground height of BuildUnderwaterPath. The Unit keeps UpdateGroundPositionZ (Map::GetHeight: static
// height and the instance's dynamic object tree, + 0.05). Map mode has no instance: the static height
// of the terrain with the same + 0.05; invalid coordinates or no height leave z as it is.
void PathInfo::srcUpdateGroundZ(float x, float y, float& z) const
{
    if (m_sourceUnit)
    {
        m_sourceUnit->UpdateGroundPositionZ(x, y, z);
        return;
    }

    if (!MaNGOS::IsValidMapCoord(x, y, z))
        return;
    float const height = m_terrain.get()->GetHeightStatic(x, y, z, true);
    if (height > INVALID_HEIGHT)
        z = height + 0.05f;
}

// Tel'Abim rule in map mode: "a player standing at start". The terrain is only asked on a map that has
// an entry (map 1), once. For the bots' callers this is result-neutral: the shortcut and NOPATH both
// come back as no path from getPathStepFrom.
bool PathInfo::MapModeInNoMovementArea(Vector3 const& start) const
{
    bool zoneRead = false;
    uint32 zoneId = 0, areaId = 0;
    for (restricted_movement_t const& t : NoMovementAreas)
    {
        if (std::get<0>(t) != m_params.mapId)
            continue;
        if (!zoneRead)
        {
            m_terrain.get()->GetZoneAndAreaId(zoneId, areaId, start.x, start.y, start.z);
            zoneRead = true;
        }
        if (std::get<1>(t) == areaId && std::get<2>(t) == zoneId)
            return true;
    }
    return false;
}

// One count per call that failed closed; the first one of each reason since the start also writes a line.
void PathInfo::CountUnitlessReject(path_unitless::Reason reason) const
{
    if (s_unitlessRejects.Add(reason))
        sLog.outError("[PathUnitless] reject=%s map=%u instance=%u (first of this reason; counts in the minute line)",
            path_unitless::ReasonName(reason), m_params.mapId, m_params.instanceId);
}

// Fail closed in calculate(Vector3...): the hole-in-the-mesh result (two-point shortcut marked NOPATH)
// that every caller already handles, instead of the null dereference before #592.
bool PathInfo::RejectUnitless(path_unitless::Reason reason, Vector3 const& start, Vector3 const& dest)
{
    CountUnitlessReject(reason);
    setEndPosition(dest);
    setStartPosition(start);
    BuildShortcut();
    m_type = PATHFIND_NOPATH;
    return true;
}

void PathInfo::ReportUnitlessRejects()
{
    std::string const line = path_unitless::FormatMinuteLine(s_unitlessRejects.Take());
    if (!line.empty())
        sLog.outString("%s", line.c_str());
}

////////////////// PathInfo //////////////////
PathInfo::PathInfo(const Unit* owner) :
    m_polyLength(0), m_type(PATHFIND_BLANK), m_useStraightPath(false), m_forceDestination(false),
    m_pointPathLimit(MAX_POINT_PATH_LENGTH), m_transport(nullptr), m_sourceUnit(owner),
    m_navMesh(nullptr), m_navMeshQuery(nullptr), m_targetAllowedFlags(0)
{
    //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::PathInfo for %u \n", m_sourceUnit->GetGUIDLow());
    createFilter();
}

// twow-repo#592 (PATH-02): map mode for the bots' map-wide paths (generator, routes without a bot on
// the start map). Before #592 this constructor dropped mapId and instanceId and the first calculate()
// dereferenced the missing Unit. With PathFinder.Unitless = 1 it computes with the player defaults of
// PathParams on mapId; with 0 (default) every calculate() fails closed with NOPATH.
PathInfo::PathInfo(uint32 mapId, uint32 instanceId) : PathInfo((Unit const*)nullptr)
{
    m_params.mapId = mapId;
    m_params.instanceId = instanceId;
    m_mapMode = sWorld.getConfig(CONFIG_BOOL_PATHFINDER_UNITLESS);
}


PathInfo::~PathInfo()
{
    //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathInfo::~PathInfo() for %u \n", m_sourceUnit->GetGUID());
}

void PathInfo::setPathLengthLimit(float dist)
{
    m_pointPathLimit = std::min<uint32>(MAX_POINT_PATH_LENGTH, uint32(dist / SMOOTH_PATH_STEP_SIZE));
}

bool PathInfo::calculate(float destX, float destY, float destZ, bool forceDest, bool offsets)
{
    // twow-repo#592: starts at the owner's position, which does not exist without a Unit (fail closed).
    if (!m_sourceUnit)
    {
        CountUnitlessReject(path_unitless::REJECT_CALC3);
        clear();
        m_type = PATHFIND_NOPATH;
        return false;
    }

    float x, y, z;
    m_sourceUnit->GetSafePosition(x, y, z, m_transport);

    return calculate(Vector3(x, y, z), Vector3(destX, destY, destZ), forceDest, offsets);
}

bool PathInfo::calculate(Vector3 const& start, Vector3 dest, bool forceDest, bool offsets)
{
    // twow-repo#592: without a Unit only map mode computes, and not on a transport (passenger
    // coordinates belong to a Unit). Everything else fails closed before the first owner read.
    if (!m_sourceUnit && !m_mapMode)
        return RejectUnitless(path_unitless::REJECT_DISABLED, start, dest);
    if (!m_sourceUnit && m_transport)
        return RejectUnitless(path_unitless::REJECT_TRANSPORT, start, dest);

    // A m_navMeshQuery object is not thread safe, but a same PathInfo can be shared between threads.
    // So need to get a new one.
    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    if (m_transport)
    {
        if (!offsets)
            m_transport->CalculatePassengerOffset(dest.x, dest.y, dest.z);

        m_navMeshQuery = mmap->GetModelNavMeshQuery(m_transport->GetDisplayId());
    }
    else
        m_navMeshQuery = mmap->GetNavMeshQuery(srcMapId());

    if (m_navMeshQuery)
        m_navMesh = m_navMeshQuery->getAttachedNavMesh();

    m_pathPoints.clear();

    Vector3 oldDest = getEndPosition();
    setEndPosition(dest);
    setStartPosition(start);

    m_forceDestination = forceDest;
    m_type = PATHFIND_BLANK;

    //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::calculate() for %u \n", m_sourceUnit->GetGUIDLow());

    // make sure navMesh works - we can run on map w/o mmap
    // check if the start and end point have a .mmtile loaded (can we pass via not loaded tile on the way?)
    if (!m_navMesh || !m_navMeshQuery || srcIgnorePathfinding() ||
            !HaveTiles(start) || !HaveTiles(dest))
    {
        BuildShortcut();
        m_type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
        return true;
    }

    // twow-repo#592: map mode reads the terrain from here on (IsSwimmable, getLiquidStatus, heights,
    // Tel'Abim). Looked up once, after the early exit above, so a map without navmesh tiles never asks;
    // never inserted (FindTerrainRef). A Unit never enters this block.
    if (!m_sourceUnit && !m_terrain.get())
    {
        m_terrain.Adopt(sTerrainMgr.FindTerrainRef(m_params.mapId));
        if (!m_terrain.get())
            return RejectUnitless(path_unitless::REJECT_NO_TERRAIN, start, dest);
    }

    updateFilter();

    // check if destination moved - if not we can optimize something here
    // we are following old, precalculated path?
    float dist = srcBoundingRadius();
    if (inRange(oldDest, dest, dist, dist) && m_pathPoints.size() > 2)
    {
        // our target is not moving - we just coming closer
        // we are moving on precalculated path - enjoy the ride
        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::calculate:: precalculated path\n");
        m_pathPoints.erase(m_pathPoints.begin());
        return false;
    }
    else
    {
        // target moved, so we need to update the poly path
        auto const buildStart = std::chrono::steady_clock::now();
        BuildPolyPath(start, dest);
        ++PathFinderStats::builds;
        PathFinderStats::buildMs += uint32(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - buildStart).count());

        if (m_type & PATHFIND_NOPATH)
        {
            const Unit* owner = m_sourceUnit; // to pull into lambda without pulling in the full member.

            // twow-repo#592: without a Unit (map mode) the rule is read at the start point; the Unit keeps the lambda.
            bool ignoreMmaps = !owner ? MapModeInNoMovementArea(start) :
                std::find_if(std::begin(NoMovementAreas), std::end(NoMovementAreas), [owner](const restricted_movement_t& t)
                {
                    const auto& [mapId, areaId, zoneId] = t;

                    uint32 ownerZoneId = 0, ownerAreaId = 0;
                    owner->GetZoneAndAreaId(ownerZoneId, ownerAreaId);

                    if (owner->GetMapId() == mapId && ownerZoneId == zoneId && ownerAreaId == areaId)
                        return true;

                    return false;
                }) != std::end(NoMovementAreas);

            if (ignoreMmaps)
            {
                BuildShortcut();
                m_type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
                return true;
            }
        }
        return true;
    }
}

dtPolyRef PathInfo::FindWalkPoly(dtNavMeshQuery const* query, float const* pointYZX, dtQueryFilter const& filter, float* closestPointYZX, float zSearchDist)
{
    ASSERT(query);

    // WARNING : Nav mesh coords are Y, Z, X (and not X, Y, Z)
    float extents[3] = {5.0f, zSearchDist, 5.0f};
    dtPolyRef polyRef;

    // Default recastnavigation method
    if (dtStatusFailed(query->findNearestPoly(pointYZX, extents, &filter, &polyRef, closestPointYZX)))
        return 0;
    // Do not select points over player pos
    if (closestPointYZX[1] > pointYZX[1] + 3.0f)
        return 0;
    return polyRef;
}

dtPolyRef PathInfo::getPolyByLocation(const float* point, float *distance, uint32 allowedFlags)
{
    float closestPoint[VERTEX_SIZE] = {0.0f, 0.0f, 0.0f};
    dtQueryFilter filter;
    filter.setIncludeFlags(m_filter.getIncludeFlags() | allowedFlags);
    dtPolyRef polyRef = FindWalkPoly(m_navMeshQuery, point, filter, closestPoint);
    if (polyRef != INVALID_POLYREF)
    {
        *distance = dtVdist(closestPoint, point);
        return polyRef;
    }
    return INVALID_POLYREF;
}

void PathInfo::BuildPolyPath(const Vector3 &startPos, const Vector3 &endPos)
{
    // *** getting start/end poly logic ***

    float distToStartPoly, distToEndPoly;
    float startPoint[VERTEX_SIZE] = {startPos.y, startPos.z, startPos.x};
    float endPoint[VERTEX_SIZE] = {endPos.y, endPos.z, endPos.x};

    bool const canSwimToDestination = srcCanSwim() &&
        srcTerrain()->IsSwimmable(startPos.x, startPos.y, startPos.z) &&
        srcTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z);

    // First case : easy flying / swimming
    if (canSwimToDestination || srcCanFly())
    {
        if (!srcFindCollision(startPos, endPos))
        {
            if (canSwimToDestination)
                BuildUnderwaterPath();
            else
            {
                BuildShortcut();
                m_type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH | PATHFIND_FLYPATH);
            }
            return;
        }
        else if (srcCanFly())
            m_forceDestination = true;
    }
    dtPolyRef startPoly = getPolyByLocation(startPoint, &distToStartPoly);
    dtPolyRef endPoly = getPolyByLocation(endPoint, &distToEndPoly, m_targetAllowedFlags);

    // we have a hole in our mesh
    // make shortcut path and mark it as NOPATH ( with flying exception )
    // its up to caller how he will use this info
    if (startPoly == INVALID_POLYREF || endPoly == INVALID_POLYREF)
    {
        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPoly == 0 || endPoly == 0)\n");
        BuildShortcut();
        // Check for swimming or flying shortcut
        if ((startPoly == INVALID_POLYREF && srcTerrain()->IsSwimmable(startPos.x, startPos.y, startPos.z)) ||
            (endPoly == INVALID_POLYREF && srcTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z)))
            m_type = srcCanSwim() ? PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH) : PATHFIND_NOPATH;
        else
            m_type = srcCreatureCanFly() ? PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH | PATHFIND_FLYPATH) : PATHFIND_NOPATH;
        return;
    }

    // we may need a better number here
    bool farFromPoly = ((distToStartPoly > 7.0f) || (distToEndPoly > 7.0f));
    if (farFromPoly)
    {
        if (canSwimToDestination)
        {
            BuildUnderwaterPath();
            return;
        }

        if (srcCanFly())
        {
            BuildShortcut();
            m_type = PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH);
            return;
        }

        float closestPoint[VERTEX_SIZE];
        if (dtStatusSucceed(m_navMeshQuery->closestPointOnPolyBoundary(endPoly, endPoint, closestPoint)))
        {
            dtVcopy(endPoint, closestPoint);
            setActualEndPosition(Vector3(endPoint[2], endPoint[0], endPoint[1]));
        }

        if (!(srcCanSwim() && srcTerrain()->IsSwimmable(m_actualEndPosition.x, m_actualEndPosition.y, m_actualEndPosition.z)))
            m_type = PATHFIND_INCOMPLETE;
    }

    // *** poly path generating logic ***

    // look for startPoly/endPoly in current path
    // TODO: we can merge it with getPathPolyByPosition() loop
    bool startPolyFound = false;
    bool endPolyFound = false;
    uint32 pathStartIndex = 0;
    uint32 pathEndIndex = 0;

    if (m_polyLength)
    {
        for (pathStartIndex = 0; pathStartIndex < m_polyLength; ++pathStartIndex)
        {
            // here to carch few bugs
            MANGOS_ASSERT(m_pathPolyRefs[pathStartIndex] != INVALID_POLYREF);

            if (m_pathPolyRefs[pathStartIndex] == startPoly)
            {
                startPolyFound = true;
                break;
            }
        }

        for (pathEndIndex = m_polyLength - 1; pathEndIndex > pathStartIndex; --pathEndIndex)
            if (m_pathPolyRefs[pathEndIndex] == endPoly)
            {
                endPolyFound = true;
                break;
            }
    }

    if (startPolyFound && endPolyFound)
    {
        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPolyFound && endPolyFound)\n");

        // we moved along the path and the target did not move out of our old poly-path
        // our path is a simple subpath case, we have all the data we need
        // just "cut" it out

        m_polyLength = pathEndIndex - pathStartIndex + 1;
        memmove(m_pathPolyRefs, m_pathPolyRefs + pathStartIndex, m_polyLength * sizeof(dtPolyRef));
    }
    else if (startPolyFound && !endPolyFound)
    {
        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (startPolyFound && !endPolyFound)\n");

        // we are moving on the old path but target moved out
        // so we have atleast part of poly-path ready

        m_polyLength -= pathStartIndex;

        // try to adjust the suffix of the path instead of recalculating entire length
        // at given interval the target cannot get too far from its last location
        // thus we have less poly to cover
        // sub-path of optimal path is optimal

        // take ~80% of the original length
        // TODO : play with the values here
        uint32 prefixPolyLength = uint32(m_polyLength * 0.8f + 0.5f);
        memmove(m_pathPolyRefs, m_pathPolyRefs + pathStartIndex, prefixPolyLength * sizeof(dtPolyRef));

        dtPolyRef suffixStartPoly = m_pathPolyRefs[prefixPolyLength - 1];

        // we need any point on our suffix start poly to generate poly-path, so we need last poly in prefix data
        float suffixEndPoint[VERTEX_SIZE];
        bool PosOverBody = false;
        if (dtStatusFailed(m_navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint, &PosOverBody)))
        {
            // we can hit offmesh connection as last poly - closestPointOnPoly() don't like that
            // try to recover by using prev polyref
            --prefixPolyLength;
            suffixStartPoly = m_pathPolyRefs[prefixPolyLength - 1];
            if (dtStatusFailed(m_navMeshQuery->closestPointOnPoly(suffixStartPoly, endPoint, suffixEndPoint, &PosOverBody)))
            {
                // suffixStartPoly is still invalid, error state
                BuildShortcut();
                m_type = PATHFIND_NOPATH;
                return;
            }
        }

        // generate suffix
        uint32 suffixPolyLength = 0;
        dtStatus dtResult = m_navMeshQuery->findPath(
                                suffixStartPoly,    // start polygon
                                endPoly,            // end polygon
                                suffixEndPoint,     // start position
                                endPoint,           // end position
                                &m_filter,            // polygon search filter
                                m_pathPolyRefs + prefixPolyLength - 1,    // [out] path
                                (int*)&suffixPolyLength,
                                MAX_PATH_LENGTH - prefixPolyLength); // max number of polygons in output path

        if (!suffixPolyLength || dtStatusFailed(dtResult))
        {
            // this is probably an error state, but we'll leave it
            // and hopefully recover on the next Update
            // we still need to copy our preffix
            sLog.outError("%u's Path Build failed: 0 length path r=0x%x", srcLogGuid(), dtResult);
        }

        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++  m_polyLength=%u prefixPolyLength=%u suffixPolyLength=%u \n",m_polyLength, prefixPolyLength, suffixPolyLength);

        // new path = prefix + suffix - overlap
        m_polyLength = prefixPolyLength + suffixPolyLength - 1;
    }
    else
    {
        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ BuildPolyPath :: (!startPolyFound && !endPolyFound)\n");

        // either we have no path at all -> first run
        // or something went really wrong -> we aren't moving along the path to the target
        // just generate new path

        // free and invalidate old path data
        clear();

        dtStatus dtResult = m_navMeshQuery->findPath(
                                startPoly,          // start polygon
                                endPoly,            // end polygon
                                startPoint,         // start position
                                endPoint,           // end position
                                &m_filter,           // polygon search filter
                                m_pathPolyRefs,     // [out] path
                                (int*)&m_polyLength,
                                MAX_PATH_LENGTH);   // max number of polygons in output path

        if (!m_polyLength || dtStatusFailed(dtResult))
        {
            // only happens if we passed bad data to findPath(), or navmesh is messed up
            sLog.outError("%u's Path Build failed: 0 length path. Result=0x%x", srcLogGuid(), dtResult);
            BuildShortcut();
            m_type = PATHFIND_NOPATH;
            return;
        }
    }

    // by now we know what type of path we can get
    if (m_pathPolyRefs[m_polyLength - 1] == endPoly && !(m_type & (PATHFIND_INCOMPLETE | PATHFIND_NOPATH)))
        m_type = PATHFIND_NORMAL;
    else
        m_type = PATHFIND_INCOMPLETE;

    BuildPointPath(startPoint, endPoint, distToStartPoly, distToEndPoly);
}

void PathInfo::BuildPointPath(const float *startPoint, const float *endPoint, float distToStartPoly, float distToEndPoly)
{
    // generate the point-path out of our up-to-date poly-path
    float pathPoints[MAX_POINT_PATH_LENGTH * VERTEX_SIZE];
    uint32 pointCount = 0;
    dtStatus dtResult = DT_FAILURE;
    if (m_useStraightPath)
    {
        dtResult = m_navMeshQuery->findStraightPath(
                       startPoint,         // start position
                       endPoint,           // end position
                       m_pathPolyRefs,     // current path
                       m_polyLength,       // lenth of current path
                       pathPoints,         // [out] path corner points
                       nullptr,               // [out] flags
                       nullptr,               // [out] shortened path
                       (int*)&pointCount,
                       m_pointPathLimit);   // maximum number of points/polygons to use
    }
    else
    {
        dtResult = findSmoothPath(
                       startPoint,         // start position
                       endPoint,           // end position
                       m_pathPolyRefs,     // current path
                       m_polyLength,       // length of current path
                       pathPoints,         // [out] path corner points
                       (int*)&pointCount,
                       m_pointPathLimit);    // maximum number of points
    }

    if (pointCount < 2 || dtStatusFailed(dtResult))
    {
        // only happens if pass bad data to findStraightPath or navmesh is broken
        // single point paths can be generated here
        // TODO : check the exact cases
        //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::BuildPointPath FAILED! path sized %d returned\n", pointCount);
        BuildShortcut();
        m_type = PATHFIND_NOPATH;
        return;
    }

    m_pathPoints.resize(pointCount);
    for (uint32 i = 0; i < pointCount; ++i)
        m_pathPoints[i] = Vector3(pathPoints[i * VERTEX_SIZE + 2], pathPoints[i * VERTEX_SIZE], pathPoints[i * VERTEX_SIZE + 1]);

    // first point is always our current location - we need the next one
    setActualEndPosition(m_pathPoints[pointCount - 1]);

    // force the given destination, if needed
    bool forceDestination = (m_forceDestination && (!(m_type & PATHFIND_NORMAL) || !inRange(getEndPosition(), getActualEndPosition(), 1.0f, 1.0f)));
    if (forceDestination)
    {
        // we may want to keep partial subpath
        if (dist3DSqr(getActualEndPosition(), getEndPosition()) <
                0.3f * dist3DSqr(getStartPosition(), getEndPosition()))
        {
            setActualEndPosition(getEndPosition());
            m_pathPoints[m_pathPoints.size() - 1] = getEndPosition();
        }
        else
        {
            setActualEndPosition(getEndPosition());
            BuildShortcut();
        }

        m_type |= PATHFIND_DEST_FORCED;
        if (srcCanFly())
            m_type |= PATHFIND_FLYPATH;
    }

    //DEBUG_FILTER_LOG(LOG_FILTER_PATHFINDING, "++ PathFinder::BuildPointPath path type %d size %d poly-size %d\n", m_type, pointCount, m_polyLength);
}

void PathInfo::BuildShortcut()
{
    clear();

    // make two point path, our curr pos is the start, and dest is the end
    m_pathPoints.resize(2);

    // set start and a default next position
    m_pathPoints[0] = getStartPosition();
    m_pathPoints[1] = getActualEndPosition();

    m_type = PATHFIND_SHORTCUT;
    if (srcCanFly())
        m_type |= PATHFIND_FLYPATH | PATHFIND_NORMAL;
}

void PathInfo::BuildUnderwaterPath()
{
    clear();

    // make two point path, our curr pos is the start, and dest is the end
    m_pathPoints.resize(2);

    // set start and a default next position
    m_pathPoints[0] = getStartPosition();
    m_pathPoints[1] = getActualEndPosition();
    GridMapLiquidData liquidData;
    uint32 liquidStatus = srcTerrain()->getLiquidStatus(getActualEndPosition().x, getActualEndPosition().y, getActualEndPosition().z, MAP_ALL_LIQUIDS, &liquidData);
    // No water here ...
    if (liquidStatus == LIQUID_MAP_NO_WATER)
    {
        m_type = PATHFIND_SHORTCUT;
        if (srcCanWalk())
        {
            // Find real height
            m_type |= PATHFIND_NORMAL;
            srcUpdateGroundZ(m_pathPoints[1].x, m_pathPoints[1].y, m_pathPoints[1].z);
        }
        else
        {
            m_type |= PATHFIND_INCOMPLETE;
            m_pathPoints[1] = getStartPosition();
        }
        return;
    }
    m_type = PATHFIND_BLANK;
    if (m_pathPoints[1].z > liquidData.level)
    {
        if (!srcCanFly())
        {
            m_pathPoints[1].z = liquidData.level;
            if (m_pathPoints[1].z > (liquidData.level + 2))
                m_type |= PATHFIND_INCOMPLETE;
        }
    }
    if (!(m_type & PATHFIND_INCOMPLETE))
        m_type |= PATHFIND_NORMAL;
    m_type |= PATHFIND_UNDERWATER;
}

void PathInfo::createFilter()
{
    unsigned short includeFlags = 0x0;
    unsigned short excludeFlags = 0x0;

    if (!m_sourceUnit)
    {
        m_filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        m_filter.setExcludeFlags(0);
        return;
    }

    if (m_sourceUnit->CanWalk())
        includeFlags |= NAV_GROUND;          // walk

    if (m_sourceUnit->CanSwim())
    {
        if (m_sourceUnit->GetTypeId() == TYPEID_PLAYER)
            includeFlags |= NAV_WATER;
        else // creatures don't take environmental damage
            includeFlags |= (NAV_WATER | NAV_MAGMA | NAV_SLIME);
    }

    m_filter.setIncludeFlags(includeFlags);
    m_filter.setExcludeFlags(excludeFlags);

    updateFilter();
}

void PathInfo::updateFilter()
{
}


void PathInfo::FillTargetAllowedFlags(Unit* target)
{
    m_targetAllowedFlags = 0;
    if (target->CanSwim())
        m_targetAllowedFlags |= NAV_WATER | NAV_SLIME | NAV_MAGMA;
    if (target->CanWalk())
        m_targetAllowedFlags |= NAV_GROUND;
    if (!target->IsPlayer())
        m_targetAllowedFlags |= NAV_STEEP_SLOPES;
}

bool PathInfo::HaveTiles(const Vector3& p) const
{
    if (m_transport)
        return true;
    int tx, ty;
    float point[VERTEX_SIZE] = {p.y, p.z, p.x};

    // check if the start and end point have a .mmtile loaded
    m_navMesh->calcTileLoc(point, &tx, &ty);
    return (m_navMesh->getTileAt(tx, ty, 0) != nullptr);
}

uint32 PathInfo::fixupCorridor(dtPolyRef* path, const uint32 npath, const uint32 maxPath,
                               const dtPolyRef* visited, const uint32 nvisited)
{
    int32 furthestPath = -1;
    int32 furthestVisited = -1;

    // Find furthest common polygon.
    for (int32 i = npath - 1; i >= 0; --i)
    {
        bool found = false;
        for (int32 j = nvisited - 1; j >= 0; --j)
        {
            if (path[i] == visited[j])
            {
                furthestPath = i;
                furthestVisited = j;
                found = true;
            }
        }
        if (found)
            break;
    }

    // If no intersection found just return current path.
    if (furthestPath == -1 || furthestVisited == -1)
        return npath;

    // Concatenate paths.

    // Adjust beginning of the buffer to include the visited.
    uint32 req = nvisited - furthestVisited;
    uint32 orig = uint32(furthestPath + 1) < npath ? furthestPath + 1 : npath;
    uint32 size = npath > orig ? npath - orig : 0;
    if (req + size > maxPath)
        size = maxPath - req;

    if (size)
        memmove(path + req, path + orig, size * sizeof(dtPolyRef));

    // Store visited
    for (uint32 i = 0; i < req; ++i)
        path[i] = visited[(nvisited - 1) - i];

    return req + size;
}

int fixupShortcuts(dtPolyRef* path, int npath, dtNavMeshQuery const* navQuery)
{
    if (npath < 3)
        return npath;

    // Get connected polygons
    static const int maxNeis = 16;
    dtPolyRef neis[maxNeis];
    int nneis = 0;

    const dtMeshTile* tile = 0;
    const dtPoly* poly = 0;
    if (dtStatusFailed(navQuery->getAttachedNavMesh()->getTileAndPolyByRef(path[0], &tile, &poly)))
        return npath;

    for (unsigned int k = poly->firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
    {
        const dtLink* link = &tile->links[k];
        if (link->ref != 0)
        {
            if (nneis < maxNeis)
                neis[nneis++] = link->ref;
        }
    }

    // If any of the neighbour polygons is within the next few polygons
    // in the path, short cut to that polygon directly.
    static const int maxLookAhead = 6;
    int cut = 0;
    for (int i = dtMin(maxLookAhead, npath) - 1; i > 1 && cut == 0; i--)
    {
        for (int j = 0; j < nneis; j++)
        {
            if (path[i] == neis[j])
            {
                cut = i;
                break;
            }
        }
    }
    if (cut > 1)
    {
        int offset = cut - 1;
        npath -= offset;
        for (int i = 1; i < npath; i++)
            path[i] = path[i + offset];
    }

    return npath;
}

bool PathInfo::getSteerTarget(const float* startPos, const float* endPos, const float minTargetDist, const dtPolyRef* path, const uint32 pathSize, float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef) const
{
    // Find steer target.
    static const uint32 MAX_STEER_POINTS = 3;
    float steerPath[MAX_STEER_POINTS * VERTEX_SIZE];
    unsigned char steerPathFlags[MAX_STEER_POINTS];
    dtPolyRef steerPathPolys[MAX_STEER_POINTS];
    uint32 nsteerPath = 0;
    dtStatus dtResult = m_navMeshQuery->findStraightPath(startPos, endPos, path, pathSize,
                        steerPath, steerPathFlags, steerPathPolys, (int*)&nsteerPath, MAX_STEER_POINTS);
    if (!nsteerPath || dtStatusFailed(dtResult))
        return false;

    // Find vertex far enough to steer to.
    uint32 ns = 0;
    while (ns < nsteerPath)
    {
        // Stop at Off-Mesh link or when point is further than slop away.
        if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
                !inRangeYZX(&steerPath[ns * VERTEX_SIZE], startPos, minTargetDist, 1000.0f))
            break;
        ns++;
    }
    // Failed to find good point to steer to.
    if (ns >= nsteerPath)
        return false;

    dtVcopy(steerPos, &steerPath[ns * VERTEX_SIZE]);
    steerPos[1] = startPos[1];  // keep Z value
    steerPosFlag = steerPathFlags[ns];
    steerPosRef = steerPathPolys[ns];

    return true;
}

//Compute the cross product AB x AC
float CrossProduct(float* pointA, float* pointB, float* pointC)
{
    float AB[2];
    float AC[2];
    AB[0] = pointB[0] - pointA[0];
    AB[1] = pointB[1] - pointA[1];
    AC[0] = pointC[0] - pointA[0];
    AC[1] = pointC[1] - pointA[1];
    float cross = AB[0] * AC[1] - AB[1] * AC[0];

    return cross;
}

//Compute the distance from A to B
float Distance(float* pointA, float* pointB)
{
    float d1 = pointA[0] - pointB[0];
    float d2 = pointA[2] - pointB[2];

    return sqrt(d1 * d1 + d2 * d2);
}

float Distance2DPointToLineYZX(float* lineA, float* lineB, float* point)
{
    return std::abs(CrossProduct(lineA, lineB, point) / Distance(lineA, lineB));
}

dtStatus PathInfo::findSmoothPath(const float* startPos, const float* endPos,
                                  const dtPolyRef* polyPath, uint32 polyPathSize,
                                  float* smoothPath, int* smoothPathSize, uint32 maxSmoothPathSize)
{
    bool simplifyPath = false;
    *smoothPathSize = 0;
    uint32 nsmoothPath = 0;

    dtPolyRef polys[MAX_PATH_LENGTH];
    memcpy(polys, polyPath, sizeof(dtPolyRef)*polyPathSize);
    uint32 npolys = polyPathSize;

    float iterPos[VERTEX_SIZE], targetPos[VERTEX_SIZE];
    int32 nSkippedPoints = 0;
    if (dtStatusFailed(m_navMeshQuery->closestPointOnPolyBoundary(polys[0], startPos, iterPos)))
        return DT_FAILURE;

    if (dtStatusFailed(m_navMeshQuery->closestPointOnPolyBoundary(polys[npolys - 1], endPos, targetPos)))
        return DT_FAILURE;

    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
    nsmoothPath++;

    // Move towards target a small advancement at a time until target reached or
    // when ran out of memory to store the path.
    while (npolys && nsmoothPath < maxSmoothPathSize)
    {
        // Find location to steer towards.
        float steerPos[VERTEX_SIZE];
        unsigned char steerPosFlag;
        dtPolyRef steerPosRef = INVALID_POLYREF;

        if (!getSteerTarget(iterPos, targetPos, SMOOTH_PATH_SLOP, polys, npolys, steerPos, steerPosFlag, steerPosRef))
            break;

        bool const endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) != 0;
        bool const offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;

        // Find movement delta.
        float delta[VERTEX_SIZE];
        dtVsub(delta, steerPos, iterPos);
        float len = sqrtf(dtVdot(delta, delta));
        // If the steer target is end of path or off-mesh link, do not move past the location.
        if ((endOfPath || offMeshConnection) && len < SMOOTH_PATH_STEP_SIZE)
            len = 1.0f;
        else if (len < SMOOTH_PATH_STEP_SIZE*4)
            len = SMOOTH_PATH_STEP_SIZE / len;
        else
            len = SMOOTH_PATH_STEP_SIZE*4 / len;

        float moveTgt[VERTEX_SIZE];
        dtVmad(moveTgt, iterPos, delta, len);

        // Move
        float result[VERTEX_SIZE];
        const static uint32 MAX_VISIT_POLY = 16;
        dtPolyRef visited[MAX_VISIT_POLY];

        uint32 nvisited = 0;

        if (dtStatusFailed(m_navMeshQuery->moveAlongSurface(polys[0], iterPos, moveTgt, &m_filter, result, visited, (int*)&nvisited, MAX_VISIT_POLY)))
            return DT_FAILURE;

        npolys = fixupCorridor(polys, npolys, MAX_PATH_LENGTH, visited, nvisited);
        npolys = fixupShortcuts(polys, npolys, m_navMeshQuery);

        if (dtStatusFailed(m_navMeshQuery->getPolyHeight(polys[0], result, &result[1])))
            DEBUG_LOG("Cannot find height at position X: %f Y: %f Z: %f for %s", result[2], result[0], result[1], srcLogName());

        result[1] += 0.5f;
        dtVcopy(iterPos, result);

        // Handle end of path and off-mesh links when close enough.
        if (endOfPath && inRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
        {
            // Reached end of path.
            if (dtStatusSucceed(m_navMeshQuery->getPolyHeight(polys[0], targetPos, &targetPos[1])))
            {
                targetPos[1] += 0.5f;
                dtVcopy(iterPos, targetPos);
            }
            if (nsmoothPath < maxSmoothPathSize)
            {
                if (nSkippedPoints)
                    dtVcopy(&smoothPath[(nsmoothPath - 1) * VERTEX_SIZE], &smoothPath[nsmoothPath * VERTEX_SIZE]);
                dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                nsmoothPath++;
            }
            break;
        }
        else if (offMeshConnection && inRangeYZX(iterPos, steerPos, SMOOTH_PATH_SLOP, 1.0f))
        {
            // Advance the path up to and over the off-mesh connection.
            dtPolyRef prevRef = INVALID_POLYREF;
            dtPolyRef polyRef = polys[0];
            uint32 npos = 0;
            while (npos < npolys && polyRef != steerPosRef)
            {
                prevRef = polyRef;
                polyRef = polys[npos];
                npos++;
            }

            for (uint32 i = npos; i < npolys; ++i)
                polys[i - npos] = polys[i];

            npolys -= npos;

            // Handle the connection.
            float startPos[VERTEX_SIZE], endPos[VERTEX_SIZE];
            if (dtStatusSucceed(m_navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef, startPos, endPos)))
            {
                if (nsmoothPath < maxSmoothPathSize)
                {
                    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], startPos);
                    nsmoothPath++;
                }
                // Move position at the other side of the off-mesh link.
                dtVcopy(iterPos, endPos);

                if (dtStatusFailed(m_navMeshQuery->getPolyHeight(polys[0], iterPos, &iterPos[1])))
                    return DT_FAILURE;

                iterPos[1] += 0.2f;
            }
        }

        // Store results.
        if (nsmoothPath < maxSmoothPathSize)
        {
            // Nostalrius: do we need to store this point ? Don't use 10 points to make a straight line: 2 are enough !
            // We eventually remove the last one
            if (simplifyPath && nsmoothPath >= 2 && nSkippedPoints >= 0)
            {
                // Check z-delta. Needed ... ?
                /*
                float currentZ = iterPos[1];
                float lastZ = smoothPath[(nsmoothPath - 1)* VERTEX_SIZE + 1];
                float lastLastZ = smoothPath[(nsmoothPath - 2)* VERTEX_SIZE + 1];*/
                if (nSkippedPoints < 20 &&  // Prevent infinite loop here
                        Distance2DPointToLineYZX(&smoothPath[(nsmoothPath - 2)* VERTEX_SIZE], &smoothPath[(nsmoothPath - 1)* VERTEX_SIZE], iterPos) < 0.8f) // Replace the last point.
                {
                    dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
                    ++nSkippedPoints;
                    continue;
                }
                else if (nSkippedPoints)
                    dtVcopy(&smoothPath[(nsmoothPath - 1) * VERTEX_SIZE], &smoothPath[nsmoothPath * VERTEX_SIZE]);
            }
            nSkippedPoints = 0;
            dtVcopy(&smoothPath[nsmoothPath * VERTEX_SIZE], iterPos);
            nsmoothPath++;
        }
    }

    *smoothPathSize = nsmoothPath;

    // this is most likely a loop
    return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : DT_FAILURE;
}

// Nostalrius
bool PathInfo::UpdateForCaster(Unit* pTarget, float castRange)
{
    // twow-repo#592: core callers only, always with a Unit; without one fail closed, path unchanged.
    if (!m_sourceUnit)
    {
        CountUnitlessReject(path_unitless::REJECT_CORE_ONLY);
        return false;
    }

    // If already in range and LOS
    if (pTarget->IsWithinDist3d(m_sourceUnit->GetPositionX(), m_sourceUnit->GetPositionY(), m_sourceUnit->GetPositionZ(), castRange) &&
            pTarget->IsWithinLOS(m_sourceUnit->GetPositionX(), m_sourceUnit->GetPositionY(), m_sourceUnit->GetPositionZ()))
    {
        clear();
        m_type = PathType(PATHFIND_SHORTCUT | PATHFIND_NORMAL | PATHFIND_CASTER);
        m_pathPoints.resize(2);
        m_pathPoints[0] = getStartPosition();
        m_pathPoints[1] = getStartPosition();
        return true;
    }
    uint32 maxIndex = m_pathPoints.size() - 1;
    // We have always keep at least 2 points (else, there is no mvt !)
    for (uint32 i = 1; i <= maxIndex; ++i)
    {
        if (pTarget->IsWithinDist3d(m_pathPoints[i].x, m_pathPoints[i].y, m_pathPoints[i].z, castRange) &&
                pTarget->IsWithinLOS(m_pathPoints[i].x, m_pathPoints[i].y, m_pathPoints[i].z))
        {
            Vector3 startPoint = m_pathPoints[i - 1];
            Vector3 endPoint = m_pathPoints[i];
            Vector3 dirVect = endPoint - startPoint;
            float targetDist1 = pTarget->GetDistance(m_pathPoints[i].x, m_pathPoints[i].y, m_pathPoints[i].z);
            float targetDist2 = pTarget->GetDistance(m_pathPoints[i - 1].x, m_pathPoints[i - 1].y, m_pathPoints[i - 1].z);
            if ((targetDist2 > targetDist1) && (targetDist2 > castRange))
            {
                float nonInRangeDist = (targetDist2 - castRange / targetDist2 - targetDist1);
                float directionLength = sqrt(dirVect.squaredLength());
                // Thales not applicable but still a valid start point due to conditions.
                startPoint += dirVect * nonInRangeDist / directionLength;
            }

            if (pTarget->IsWithinDist3d(startPoint.x, startPoint.y, startPoint.z, castRange) &&
                    pTarget->IsWithinLOS(startPoint.x, startPoint.y, startPoint.z))
                m_pathPoints[i] = startPoint;
            m_pathPoints.resize(i + 1);

            return false;
        }
    }
    return false;
}


bool PathInfo::UpdateForMelee(Unit* pTarget, float meleeReach)
{
    // twow-repo#592: core callers only, always with a Unit; without one fail closed, path unchanged.
    if (!m_sourceUnit)
    {
        CountUnitlessReject(path_unitless::REJECT_CORE_ONLY);
        return false;
    }

    // Si deja en ligne de vision, et a distance, c'est bon.
    if (pTarget->IsWithinDist3d(m_sourceUnit->GetPositionX(), m_sourceUnit->GetPositionY(), m_sourceUnit->GetPositionZ(), meleeReach))
    {
        clear();
        m_type = PathType(PATHFIND_SHORTCUT | PATHFIND_NORMAL | PATHFIND_CASTER);
        m_pathPoints.resize(2);
        m_pathPoints[0] = getStartPosition();
        m_pathPoints[1] = getStartPosition();
        return true;
    }

    uint32 maxIndex = m_pathPoints.size() - 1;
    // We have always keep at least 2 points (else, there is no mvt !)
    for (uint32 i = 1; i <= maxIndex; ++i)
    {
        if (pTarget->IsWithinDist3d(m_pathPoints[i].x, m_pathPoints[i].y, m_pathPoints[i].z, meleeReach))
        {
            Vector3 dirVect;
            pTarget->GetPosition(dirVect.x, dirVect.y, dirVect.z);
            dirVect -= m_pathPoints[i - 1];
            float targetDist = pTarget->GetDistance(m_pathPoints[i - 1].x, m_pathPoints[i - 1].y, m_pathPoints[i - 1].z) - meleeReach;
            float directionLength = sqrt(dirVect.squaredLength());
            m_pathPoints[i] = m_pathPoints[i - 1] + dirVect * targetDist / directionLength;
            m_pathPoints.resize(i + 1);
            return false;
        }
    }
    return false;
}


void PathInfo::CutPathWithDynamicLoS()
{
    // twow-repo#592: core callers only, always with a Unit; without one fail closed, path unchanged.
    if (!m_sourceUnit)
    {
        CountUnitlessReject(path_unitless::REJECT_CORE_ONLY);
        return;
    }

    uint32 maxIndex = m_pathPoints.size() - 1;
    Vector3 out;
    // We have always keep at least 2 points (else, there is no mvt !)
    for (uint32 i = 1; i <= maxIndex; ++i)
        if (m_sourceUnit->GetMap()->GetDynamicObjectHitPos(m_pathPoints[i - 1], m_pathPoints[i], out, -0.1f))
        {
            m_pathPoints[i] = out;
            m_pathPoints.resize(i + 1);
            break;
        }
}

float PathInfo::Length() const
{
    ASSERT(m_pathPoints.size());
    float length = 0.0f;
    uint32 maxIndex = m_pathPoints.size() - 1;
    for (uint32 i = 1; i <= maxIndex; ++i)
        length += (m_pathPoints[i - 1] - m_pathPoints[i]).length();
    return length;
}

bool PathInfo::inRangeYZX(const float* v1, const float* v2, float r, float h)
{
    const float dx = v2[0] - v1[0];
    const float dy = v2[1] - v1[1]; // elevation
    const float dz = v2[2] - v1[2];
    return (dx * dx + dz * dz) < r * r && fabsf(dy) < h;
}

bool PathInfo::inRange(const Vector3 &p1, const Vector3 &p2, float r, float h)
{
    Vector3 d = p1 - p2;
    return (d.x * d.x + d.y * d.y) < r * r && fabsf(d.z) < h;
}

float PathInfo::dist3DSqr(const Vector3 &p1, const Vector3 &p2)
{
    return (p1 - p2).squaredLength();
}
