if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#592 (PATH-02 variant A, PR pair 2): PathInfo without a Unit. Every read of the owner on the
# calculate() path goes through an accessor "m_sourceUnit ? <the expression used before> : m_params.x",
# so the paths of all units stay byte-equal (review checklist B1-B6 in the PR text, proof by the
# .mmap replay old = new). PathInfo(mapId, instanceId) keeps mapId/instanceId and computes in map mode
# with PathFinder.Unitless = 1; everything else without a Unit fails closed with NOPATH and a counter.
# No snapshot mode, no preload default, no CleanUpGrids lock, IsMMapIsLoaded stays a stub.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#592: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#592: forbidden ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#592: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Body of one function: from its signature to the first closing brace at column 0.
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "#592: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if (end EQUAL -1)
    message(FATAL_ERROR "#592: end of function not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(require_count text needle expected label)
  string(REGEX MATCHALL "${needle}" hits "${text}")
  list(LENGTH hits count)
  if (NOT count EQUAL ${expected})
    message(FATAL_ERROR "#592: ${label}: expected ${expected}, found ${count}")
  endif()
endfunction()

read_source("src/game/Maps/PathFinder.h" pf_h)
read_source("src/game/Maps/PathFinder.cpp" pf_cpp)
read_source("src/game/Maps/PathUnitless.h" policy)
read_source("src/game/Maps/GridMap.h" grid_h)
read_source("src/game/Maps/GridMap.cpp" grid_cpp)
read_source("src/game/World.h" world_h)
read_source("src/game/World.cpp" world_cpp)
read_source("src/mangosd/mangosd.conf.dist.in" conf)

# 1. Switch: declared, read with default off, documented off.
require_text("${world_h}" "CONFIG_BOOL_PATHFINDER_UNITLESS," "config enum entry")
require_text("${world_cpp}" "setConfig(CONFIG_BOOL_PATHFINDER_UNITLESS, \"PathFinder.Unitless\", false);" "config key, default false")
require_text("${conf}" "\nPathFinder.Unitless = 0\n" "documented off in mangosd.conf.dist.in")
function_body("${pf_cpp}" "PathInfo::PathInfo(uint32 mapId, uint32 instanceId) : PathInfo((Unit const*)nullptr)" body)
require_text("${body}" "m_params.mapId = mapId;" "map mode keeps mapId (dropped before #592)")
require_text("${body}" "m_params.instanceId = instanceId;" "map mode keeps instanceId")
require_text("${body}" "m_mapMode = sWorld.getConfig(CONFIG_BOOL_PATHFINDER_UNITLESS);" "map mode only with the switch")
forbid_text("${pf_h}" "PathInfo(uint32 /*mapId*/, uint32 /*instanceId*/)" "the old map constructor that dropped mapId")
require_text("${pf_h}" "        bool            m_mapMode = false;" "map mode off unless the constructor sets it")
require_text("${pf_h}" "        bool IsUnitless() const { return !m_sourceUnit; }" "IsUnitless for the bot check T2")

# 2. PathParams: the player defaults of map mode.
require_text("${pf_h}" "    bool canWalk = true;" "default walk")
require_text("${pf_h}" "    bool canSwim = true;" "default swim")
require_text("${pf_h}" "    bool canFly = false;" "default no flying")
require_text("${pf_h}" "    bool creatureCanFly = false;" "default no creature flying")
require_text("${pf_h}" "    bool ignorePathfinding = false;" "default pathfinding on")

# 3. B1-B3: the accessors, left of ':' exactly the expression used before #592.
require_text("${pf_cpp}" "inline uint32 PathInfo::srcMapId() const { return m_sourceUnit ? m_sourceUnit->GetMapId() : m_params.mapId; }" "accessor mapId")
require_text("${pf_cpp}" "inline bool PathInfo::srcIgnorePathfinding() const { return m_sourceUnit ? m_sourceUnit->HasUnitState(UNIT_STAT_IGNORE_PATHFINDING) : m_params.ignorePathfinding; }" "accessor ignore pathfinding")
require_text("${pf_cpp}" "inline float PathInfo::srcBoundingRadius() const { return m_sourceUnit ? m_sourceUnit->GetObjectBoundingRadius() : m_params.boundingRadius; }" "accessor bounding radius")
require_text("${pf_cpp}" "inline bool PathInfo::srcCanSwim() const { return m_sourceUnit ? m_sourceUnit->CanSwim() : m_params.canSwim; }" "accessor swim")
require_text("${pf_cpp}" "inline bool PathInfo::srcCanFly() const { return m_sourceUnit ? m_sourceUnit->CanFly() : m_params.canFly; }" "accessor fly")
require_text("${pf_cpp}" "inline bool PathInfo::srcCanWalk() const { return m_sourceUnit ? m_sourceUnit->CanWalk() : m_params.canWalk; }" "accessor walk")
require_text("${pf_cpp}" "inline bool PathInfo::srcCreatureCanFly() const { return m_sourceUnit ? (m_sourceUnit->GetTypeId() == TYPEID_UNIT && ((Creature*)m_sourceUnit)->CanFly()) : m_params.creatureCanFly; }" "accessor creature fly (short circuit kept)")
require_text("${pf_cpp}" "inline TerrainInfo const* PathInfo::srcTerrain() const { return m_sourceUnit ? m_sourceUnit->GetTerrain() : m_terrain.get(); }" "accessor terrain (GetTerrain on every read, with its assert)")
require_text("${pf_cpp}" "inline uint32 PathInfo::srcLogGuid() const { return m_sourceUnit ? m_sourceUnit->GetGUIDLow() : m_params.logGuidLow; }" "accessor log guid")
require_text("${pf_cpp}" "inline char const* PathInfo::srcLogName() const { return m_sourceUnit ? m_sourceUnit->GetName() : \"<no unit>\"; }" "accessor log name (DEBUG_LOG)")
# The inline accessors are declared inline in the class too ([dcl.inline]: inline in every TU that declares them).
foreach(decl
    "uint32 srcMapId() const;" "bool srcIgnorePathfinding() const;" "float srcBoundingRadius() const;"
    "bool srcCanSwim() const;" "bool srcCanFly() const;" "bool srcCanWalk() const;" "bool srcCreatureCanFly() const;"
    "TerrainInfo const* srcTerrain() const;" "uint32 srcLogGuid() const;" "char const* srcLogName() const;")
  require_text("${pf_h}" "        inline ${decl}" "accessor declared inline")
endforeach()
read_source("src/game/CMakeLists.txt" game_cmake)
require_text("${game_cmake}" "    Maps/PathUnitless.h\n" "PathUnitless.h in the game header list")

# B2: collision and ground height stay on the Unit's map.
function_body("${pf_cpp}" "bool PathInfo::srcFindCollision(Vector3 const& start, Vector3 const& end) const" body)
require_order("${body}" "    if (m_sourceUnit)\n        return m_sourceUnit->GetMap()->FindCollisionModel(start.x, start.y, start.z, end.x, end.y, end.z) != nullptr;" "VMapFactory::createOrGetVMapManager()->FindCollisionModel(m_params.mapId," "Unit keeps Map::FindCollisionModel, map mode asks the VMapManager with mapId")
function_body("${pf_cpp}" "void PathInfo::srcUpdateGroundZ(float x, float y, float& z) const" body)
require_order("${body}" "    if (m_sourceUnit)\n    {\n        m_sourceUnit->UpdateGroundPositionZ(x, y, z);\n        return;\n    }" "GetHeightStatic(x, y, z, true)" "Unit keeps UpdateGroundPositionZ, map mode the static height")

# B1: short circuits in BuildPolyPath unchanged (IsSwimmable loads grids, vmaps and mmap tiles).
function_body("${pf_cpp}" "void PathInfo::BuildPolyPath(const Vector3 &startPos, const Vector3 &endPos)" body)
require_text("${body}" "    bool const canSwimToDestination = srcCanSwim() &&\n        srcTerrain()->IsSwimmable(startPos.x, startPos.y, startPos.z) &&\n        srcTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z);" "swim short circuit (CanSwim first)")
require_text("${body}" "    if (canSwimToDestination || srcCanFly())\n    {\n        if (!srcFindCollision(startPos, endPos))" "first case")
require_text("${body}" "        if ((startPoly == INVALID_POLYREF && srcTerrain()->IsSwimmable(startPos.x, startPos.y, startPos.z)) ||\n            (endPoly == INVALID_POLYREF && srcTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z)))\n            m_type = srcCanSwim() ? PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH) : PATHFIND_NOPATH;\n        else\n            m_type = srcCreatureCanFly() ?" "hole in the mesh short circuit")
require_text("${body}" "        if (!(srcCanSwim() && srcTerrain()->IsSwimmable(m_actualEndPosition.x, m_actualEndPosition.y, m_actualEndPosition.z)))" "far from poly short circuit")
require_text("${body}" "sLog.outError(\"%u's Path Build failed: 0 length path r=0x%x\", srcLogGuid(), dtResult);" "log text unchanged")
require_text("${body}" "sLog.outError(\"%u's Path Build failed: 0 length path. Result=0x%x\", srcLogGuid(), dtResult);" "log text unchanged")

# 4. calculate(): fail closed first, the terrain after the early exit, dead branch dead (B5), Tel'Abim (B3).
function_body("${pf_cpp}" "bool PathInfo::calculate(float destX, float destY, float destZ, bool forceDest, bool offsets)" body)
require_order("${body}" "CountUnitlessReject(path_unitless::REJECT_CALC3);" "m_sourceUnit->GetSafePosition(x, y, z, m_transport);" "calc3 fails closed before the owner position")
function_body("${pf_cpp}" "bool PathInfo::calculate(Vector3 const& start, Vector3 dest, bool forceDest, bool offsets)" calc)
require_text("${calc}" "    if (!m_sourceUnit && !m_mapMode)\n        return RejectUnitless(path_unitless::REJECT_DISABLED, start, dest);\n    if (!m_sourceUnit && m_transport)\n        return RejectUnitless(path_unitless::REJECT_TRANSPORT, start, dest);" "disabled and transport fail closed")
require_order("${calc}" "REJECT_TRANSPORT" "MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();" "rejects before the first navmesh or owner read")
require_text("${calc}" "        m_navMeshQuery = mmap->GetNavMeshQuery(srcMapId());" "navmesh of the owner's map")
require_order("${calc}" "    if (!m_navMesh || !m_navMeshQuery || srcIgnorePathfinding() ||" "    if (!m_sourceUnit && !m_terrain.get())" "terrain only after the no-navmesh/no-tiles early exit")
require_order("${calc}" "        m_terrain.Adopt(sTerrainMgr.FindTerrainRef(m_params.mapId));" "    updateFilter();" "terrain before the first terrain read")
require_text("${calc}" "            return RejectUnitless(path_unitless::REJECT_NO_TERRAIN, start, dest);" "missing terrain fails closed")
require_order("${calc}" "    m_pathPoints.clear();" "m_pathPoints.size() > 2" "B5: the precalculated-path branch stays dead")
require_text("${calc}" "    float dist = srcBoundingRadius();\n    if (inRange(oldDest, dest, dist, dist) && m_pathPoints.size() > 2)\n    {\n        // our target is not moving - we just coming closer\n        // we are moving on precalculated path - enjoy the ride\n" "B5: dead branch unchanged")
require_text("${calc}" "        m_pathPoints.erase(m_pathPoints.begin());\n        return false;\n    }\n    else\n    {\n        // target moved, so we need to update the poly path" "B5: dead branch unchanged")
require_text("${calc}" "            bool ignoreMmaps = !owner ? MapModeInNoMovementArea(start) :\n                std::find_if(std::begin(NoMovementAreas), std::end(NoMovementAreas), [owner](const restricted_movement_t& t)" "B3: map mode at start, the Unit keeps the lambda")
require_text("${calc}" "                    owner->GetZoneAndAreaId(ownerZoneId, ownerAreaId);\n\n                    if (owner->GetMapId() == mapId && ownerZoneId == zoneId && ownerAreaId == areaId)" "B3: lambda unchanged")

# 5. Core-only post-processing fails closed without a Unit.
foreach (sig "bool PathInfo::UpdateForCaster(Unit* pTarget, float castRange)" "bool PathInfo::UpdateForMelee(Unit* pTarget, float meleeReach)" "void PathInfo::CutPathWithDynamicLoS()")
  function_body("${pf_cpp}" "${sig}" body)
  require_order("${body}" "CountUnitlessReject(path_unitless::REJECT_CORE_ONLY);" "m_sourceUnit->" "core-only guard first: ${sig}")
endforeach()

# 6. B4: createFilter unchanged and still called in the constructor (map mode = its nullptr branch).
function_body("${pf_cpp}" "void PathInfo::createFilter()" body)
require_text("${body}" "    if (!m_sourceUnit)\n    {\n        m_filter.setIncludeFlags(NAV_GROUND | NAV_WATER);\n        m_filter.setExcludeFlags(0);\n        return;\n    }\n\n    if (m_sourceUnit->CanWalk())\n        includeFlags |= NAV_GROUND;          // walk\n\n    if (m_sourceUnit->CanSwim())\n    {\n        if (m_sourceUnit->GetTypeId() == TYPEID_PLAYER)" "createFilter unchanged")
function_body("${pf_cpp}" "PathInfo::PathInfo(const Unit* owner) :" body)
require_text("${body}" "    createFilter();" "filter built in the constructor")

# 7. Exact number of direct owner reads in PathFinder.cpp: 12 accessors + calc3 + createFilter (3) +
#    UpdateForCaster/UpdateForMelee/CutPathWithDynamicLoS (10) + 3 old comments. A new direct read
#    (outside an accessor) turns this red.
require_count("${pf_cpp}" "m_sourceUnit->" 29 "m_sourceUnit-> in PathFinder.cpp")
require_count("${pf_h}" "m_sourceUnit->" 0 "m_sourceUnit-> in PathFinder.h")

# 8. No new shared mutex, no terrain insert from PathFinder; FindTerrainRef never inserts.
forbid_text("${pf_h}" "std::shared_mutex" "a shared_mutex in PathFinder.h")
forbid_text("${pf_cpp}" "std::shared_mutex" "a shared_mutex in PathFinder.cpp")
forbid_text("${pf_h}" "LoadTerrain" "LoadTerrain in PathFinder.h")
forbid_text("${pf_cpp}" "LoadTerrain" "LoadTerrain in PathFinder.cpp (inserts; Update() iterates unguarded)")
require_text("${grid_h}" "        TerrainInfo* FindTerrainRef(const uint32 mapId);" "FindTerrainRef declared")
function_body("${grid_cpp}" "TerrainInfo* TerrainManager::FindTerrainRef(const uint32 mapId)" body)
require_order("${body}" "    Guard _guard(*this);" "i_TerrainMap.find(mapId)" "lookup under the guard")
require_order("${body}" "        return nullptr;" "    iter->second->AddRef();" "not loaded = nullptr, else one reference")
forbid_text("${body}" "i_TerrainMap[" "operator[] (inserts)")
forbid_text("${body}" "new TerrainInfo" "a new terrain")
function_body("${pf_cpp}" "PathTerrainRef::~PathTerrainRef()" body)
require_text("${body}" "m_terrain->Release();" "reference released (RAII)")

# 9. getArea stays a stub returning 0: it keeps the route part of the bots' GetFishLocations
#    unreachable. Implementing it must break this test and force a review of that part (std::async jobs,
#    node graph writes; AiPlayerbot.Perf.PathGenUnitless gate, bot contract path_unitless_592).
require_text("${pf_h}" "        float getArea(float /*x*/, float /*y*/, float /*z*/) const { return 0.0f; }" "getArea stub returns 0")
require_text("${pf_h}" "        float getArea(uint32 /*mapId*/, float /*x*/, float /*y*/, float /*z*/) const { return 0.0f; }" "getArea(mapId, ...) stub returns 0")

# 10. Diagnostics: one-time reject line per reason, a minute line only when something failed closed.
require_text("${pf_cpp}" "sLog.outError(\"[PathUnitless] reject=%s map=%u instance=%u" "one-time reject line")
function_body("${pf_cpp}" "void PathInfo::ReportUnitlessRejects()" body)
require_text("${body}" "if (!line.empty())" "no minute line without rejects")
require_text("${policy}" "\"[PathUnitless] rejects=%llu disabled=%llu calc3=%llu core_only=%llu no_terrain=%llu transport=%llu\"" "minute line format")
require_text("${policy}" "        if (!sum)\n            return std::string();" "empty when nothing failed closed")
require_text("${policy}" "std::memory_order_relaxed" "relaxed counters")
require_order("${world_cpp}" "        sObjectMgr.SaveVariables();\n" "        PathInfo::ReportUnitlessRejects();" "minute line on the once-a-minute world timer")

message(STATUS "path_unitless_592 contract passed")
