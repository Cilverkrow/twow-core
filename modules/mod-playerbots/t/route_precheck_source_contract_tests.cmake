cmake_policy(VERSION 3.16)
# twow-repo#592 (PATH-02 phase 2, group A no. 5): behind AiPlayerbot.Perf.RoutePrecheck (default 0, clamp 0..4) the end
# point pre-check of a node route build. Level 1 = shadow (count, compare, never skip), 2 skips the pair loop when no
# polygon lies within reach of the target, 3 also on the start check, 4 also the direct attempt (deferred).
# The skip is only correct while the proof holds: a target leg counts at < 1 yd (2D) / < 2 yd (Z) (getRoute,
# WorldPosition::isPathTo), a navmesh path ends on a polygon 0.5 above getPolyHeight (PathInfo::findSmoothPath), the
# start polygon comes from FindWalkPoly's 5 / 10 / 5 box, and the shortcut cases (tiles, swimming, Tel'Abim, ignore
# pathfinding) are exactly the exempt ones. This contract pins those core and module lines, so a change there breaks
# the test and forces the proof to be checked again. Pure decisions and format: route_precheck_policy_tests.

if(NOT DEFINED CORE_SOURCE_DIR)
  set(CORE_SOURCE_DIR "${PB_SOURCE_DIR}/../../../..")
endif()

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#592 RoutePrecheck: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#592 RoutePrecheck: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#592 RoutePrecheck: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#592 RoutePrecheck: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#592 RoutePrecheck: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR at "${at} + ${needle_len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/RoutePrecheckPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/TravelNode.cpp" node_cpp)
read_file("${PB_SOURCE_DIR}/WorldPosition.cpp" pos_cpp)
read_file("${PB_SOURCE_DIR}/WorldPosition.h" pos_h)
read_file("${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" move_cpp)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rnd_mgr)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_file("${CORE_SOURCE_DIR}/src/game/Maps/PathFinder.cpp" pf_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Maps/PathFinder.h" pf_h)

# 1. Switch: default 0, clamped 0..4, documented with the acceptance note.
require_text("${config_h}" "    uint32 perfRoutePrecheck = 0;  // twow-repo#592 (PATH-02)" "member default off")
require_text("${config_cpp}" "perfRoutePrecheck = ai::route_precheck::ClampLevel(config.GetIntDefault(\"AiPlayerbot.Perf.RoutePrecheck\", 0));" "load default 0, clamped")
require_text("${policy}" "constexpr std::uint32_t MaxLevel = 4;" "upper clamp")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.RoutePrecheck = 0\n" "documented off")
require_text("${conf_dist}" "enable only after OB-30 measurement + owner\n# go - nach Abnahme an; released for measuring: 1 and 2)" "acceptance note")

# 2. Proof basis in the core. findSmoothPath: target on the last polygon's boundary, end 0.5 above the polygon height.
function_body("${pf_cpp}" "dtStatus PathInfo::findSmoothPath(" smooth)
require_text("${smooth}" "if (dtStatusFailed(m_navMeshQuery->closestPointOnPolyBoundary(polys[npolys - 1], endPos, targetPos)))" "end on the last polygon")
require_text("${smooth}" "        result[1] += 0.5f;\n        dtVcopy(iterPos, result);" "steps 0.5 above the polygon")
require_text("${smooth}" "            if (dtStatusSucceed(m_navMeshQuery->getPolyHeight(polys[0], targetPos, &targetPos[1])))\n            {\n                targetPos[1] += 0.5f;" "end 0.5 above the polygon height")
require_text("${pf_cpp}" "m_polyLength(0), m_type(PATHFIND_BLANK), m_useStraightPath(false)" "smooth path by default")
string(FIND "${move_cpp}${node_cpp}${pos_cpp}" "setUseStrightPath" straight_use)
if(NOT straight_use EQUAL -1)
  message(FATAL_ERROR "#592 RoutePrecheck: a bot path uses findStraightPath now - check the end point proof")
endif()
# The shortcut cases: no navmesh / tile, ignore pathfinding (the pre-check checks the same tiles, layer 0).
# twow-repo#592 PR pair 2 (PathInfo variant A): the owner reads go through the src*() accessors, which keep the
# Unit expression verbatim (check_path_unitless_592_contract.cmake pins them).
require_text("${pf_cpp}" "    if (!m_navMesh || !m_navMeshQuery || srcIgnorePathfinding() ||\n            !HaveTiles(start) || !HaveTiles(dest))" "shortcut without tiles")
require_text("${pf_cpp}" "inline bool PathInfo::srcIgnorePathfinding() const { return m_sourceUnit ? m_sourceUnit->HasUnitState(UNIT_STAT_IGNORE_PATHFINDING)" "ignore-pathfinding accessor reads the Unit")
require_text("${pf_cpp}" "    m_navMesh->calcTileLoc(point, &tx, &ty);\n    return (m_navMesh->getTileAt(tx, ty, 0) != nullptr);" "tile test")
# Swimming / flying first, the hole shortcut only when a swimmable start or end has no polygon.
require_text("${pf_cpp}" "    bool const canSwimToDestination = srcCanSwim() &&\n        srcTerrain()->IsSwimmable(startPos.x, startPos.y, startPos.z) &&\n        srcTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z);" "swim path needs both ends swimmable")
require_text("${pf_cpp}" "inline bool PathInfo::srcCanSwim() const { return m_sourceUnit ? m_sourceUnit->CanSwim()" "swim accessor reads the Unit")
require_text("${pf_cpp}" "inline TerrainInfo const* PathInfo::srcTerrain() const { return m_sourceUnit ? m_sourceUnit->GetTerrain()" "terrain accessor reads the Unit")
require_text("${pf_cpp}" "        if ((startPoly == INVALID_POLYREF && srcTerrain()->IsSwimmable(startPos.x, startPos.y, startPos.z)) ||\n            (endPoly == INVALID_POLYREF && srcTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z)))" "hole shortcut only at swimmable ends")
require_text("${pf_cpp}" "    float extents[3] = {5.0f, zSearchDist, 5.0f};" "FindWalkPoly box")
require_text("${pf_h}" "float* closestPointYZX, float zSearchDist = 10.0f);" "FindWalkPoly default height")
# Tel'Abim: NOPATH there becomes a shortcut; the pre-check exempts the whole zone.
require_text("${pf_cpp}" "std::make_tuple(1u, 5122u, 5121u), // The Shallow Strand, Tel'Abim" "Tel'Abim area 5122")
require_text("${pf_cpp}" "std::make_tuple(1u, 5128u, 5121u), // Jagged Isles, Tel'Abim" "Tel'Abim area 5128")
require_text("${pf_cpp}" "std::make_tuple(1u, 5129u, 5121u) // The Washing Shore, Tel'Abim" "Tel'Abim area 5129")
require_text("${policy}" "constexpr std::uint32_t TelAbimMapId = 1;\nconstexpr std::uint32_t TelAbimZoneId = 5121;" "Tel'Abim exempt in the policy")
# The map query the bot paths use is the thread's own for the map (instance ignored).
require_text("${pf_cpp}" "m_navMeshQuery = mmap->GetNavMeshQuery(srcMapId());" "PathInfo queries the map's mesh")
require_text("${pf_cpp}" "inline uint32 PathInfo::srcMapId() const { return m_sourceUnit ? m_sourceUnit->GetMapId()" "map id accessor reads the Unit")

# 3. Proof basis in the module: success criteria, the underwater append, the water retries.
require_text("${pos_cpp}" "return !path.empty() && path.back().getMapId() == getMapId() && sqDistance2d(path.back()) < realMaxDistance * realMaxDistance && abs(path.back().getZ() - getZ()) < maxZDistance;" "isPathTo: 2D and Z tolerance")
require_text("${pos_h}" "bool isPathTo(const std::vector<WorldPosition>& path, float const maxDistance = 0, float const maxZDistance = 2.0f) const;" "isPathTo default Z 2 yd")
require_text("${pos_cpp}" "if (lastPoint.distance(end) < 50.0f && lastPoint.isUnderWater() && end.isUnderWater() && lastPoint.IsInLineOfSight(end))" "underwater append needs an underwater target")
function_body("${node_cpp}" "TravelNodeRoute TravelNodeMap::getRoute(WorldPosition startPos, WorldPosition endPos," route)
require_text("${route}" "                    endPath = endNodePosition.getPathTo(endPos, unit);\n\n                    bool hasPath = endPos.isPathTo(endPath, 1.0f);" "target leg: 1 yd")
require_text("${route}" "                            hasPath = surfaceEnd.isPathTo(endPath, 1.0f);" "surface retry: 1 yd")
require_text("${route}" "float maxStartDistance = startNode->isTransport() ? 20.0f : 1.0f;" "start leg reach 1 / 20 yd")
require_text("${route}" "bool hasPath = (startNodePosition.cropPathTo(newStartPath, maxStartDistance));" "start leg cropped from the direct path")
require_text("${policy}" "constexpr float EndLegReach = 1.0f;" "policy: target leg reach")
require_text("${policy}" "constexpr float PathZReach = 2.0f;" "policy: Z reach")
require_text("${policy}" "constexpr float PolyEndLift = 0.5f;" "policy: polygon lift")
require_text("${policy}" "constexpr float StartHalfXY = 5.0f + BoxMargin;" "policy: FindWalkPoly box")
require_text("${policy}" "constexpr float StartHalfZ = 10.0f + BoxMargin;" "policy: FindWalkPoly height")
require_text("${policy}" "constexpr float StartNodeReach = 1.0f;\nconstexpr float TransportStartNodeReach = 20.0f;" "policy: start node reach")

# 4. Level 1 never skips; every skip goes through Skips().
require_text("${policy}" "        case Kind::End: return level >= 2;\n        case Kind::Start: return level >= 3;\n        case Kind::Direct: return level >= 4;" "levels")
require_text("${policy}" "    if (r != Result::NoPoly)\n        return false;" "only 'no polygon' skips")
count_text("${node_cpp}" "route_precheck::Skips(" skips_calls)
if(NOT skips_calls EQUAL 3)
  message(FATAL_ERROR "#592 RoutePrecheck: expected 3 Skips() decisions (target, start, direct), found ${skips_calls}")
endif()
# Every unknown answers "no skip": exempt, tiles, polygon (a failed query = maybe), water.
require_text("${node_cpp}" "        return !dtStatusSucceed(status) || ref != INVALID_POLYREF;" "failed query = maybe a polygon")
require_text("${node_cpp}" "        filter.setIncludeFlags(0xFFFF);\n        filter.setExcludeFlags(0);" "filter passes every flag")
require_text("${node_cpp}" "    return !terrain || terrain->IsSwimmable(p.getX(), p.getY(), p.getZ()) || terrain->IsInWater(p.getX(), p.getY(), p.getZ()) ||\n        terrain->IsUnderWater(p.getX(), p.getY(), p.getZ());" "water: swimmable, in or under water")
function_body("${node_cpp}" "bool PrecheckExempt(Player* bot, WorldPosition const& startPos, WorldPosition const& endPos)" exempt)
foreach(needle "!bot" "!bot->IsAlive()" "bot->GetTransport()" "bot->CanFly()" "bot->HasUnitState(UNIT_STAT_IGNORE_PATHFINDING)"
        "bot->GetMapId() != startPos.getMapId() || startPos.getMapId() != endPos.getMapId()"
        "bot->GetZoneId() == route_precheck::TelAbimZoneId" "sTravelNodeMap.gethasToGen()" "sPlayerbotAIConfig.hasLog(\"deadzone.csv\")")
  require_text("${exempt}" "${needle}" "exemption ${needle}")
endforeach()
function_body("${node_cpp}" "route_precheck::Result PrecheckEnd(" end_check)
require_text("${end_check}" "mesh.BoxTilesLoaded(endPos, route_precheck::EndHalfXY)" "target box on loaded tiles")
require_text("${end_check}" "if (!mesh.TileLoaded(*node->getPosition()))" "end node tiles loaded")
require_text("${end_check}" "mesh.PolyNear(endPos, route_precheck::EndHalfXY, route_precheck::EndHalfZ)" "target box query")
require_text("${end_check}" "if (PrecheckWater(*node->getPosition()))" "end nodes not in water")
require_text("${end_check}" "if (node->getPosition()->sqDistance2d(endPos) <= nodeReach * nodeReach)" "end node within reach exempt")
function_body("${node_cpp}" "route_precheck::Result PrecheckStart(" start_check)
require_text("${start_check}" "    if (startPath.size() != 1)\n        return route_precheck::Result::ExemptOther;" "start check only while the direct path never left the start")
require_text("${start_check}" "mesh.PolyNear(startPos, route_precheck::StartHalfXY, route_precheck::StartHalfZ)" "start box query")
function_body("${node_cpp}" "route_precheck::Result PrecheckDirect(" direct_check)
require_text("${direct_check}" "float const reach = sPlayerbotAIConfig.spellDistance + route_precheck::BoxMargin;" "direct box from the spell distance")
require_text("${direct_check}" "mesh.TileLoaded(startPos) && mesh.BoxTilesLoaded(endPos, reach)" "direct: start tile and target box tiles")

# 5. getRoute: the checks only for the getFullPath build's own call, after the cooldown check and the candidates,
#    before the pair loop; a skip marks every start node bad (no A*, no leg), the loop itself is unchanged.
require_text("${route}" "bool const precheckOn = precheck && precheck->Matches(startPos.getMapId(), startPos.getX(), startPos.getY(), startPos.getZ(),\n        endPos.getMapId(), endPos.getX(), endPos.getY(), endPos.getZ());" "only the build's own call")
require_order("${route}" "RouteCooldowns().IsBlocked(routeKey, routeNow)" "RunRoutePrecheck(*precheck" "after the cooldown check")
require_order("${route}" "outDetail->endNodeDist = " "RunRoutePrecheck(*precheck" "after the candidates")
require_order("${route}" "RunRoutePrecheck(*precheck" "//Cycle over the combinations of these 5 nodes." "before the pair loop")
require_text("${route}" "    if (precheckOn && precheck->skipped != route_precheck::Kind::None)\n    {\n        badStartNodes = startNodes;" "skip = all start nodes bad")
require_text("${route}" "            if (std::find(badStartNodes.begin(), badStartNodes.end(), startNode) != badStartNodes.end())\n                continue;" "the loop skips bad start nodes before any A*")
require_order("${route}" "                startPath = newStartPath;\n                if (precheckOn)" "testPathToLoop(startPos, endPos, unit, uid, routePoints, \"route\");\n                }\n\n                return route;" "wrong counted on a found route")
# The hearthstone fallback and the route-fail cooldown stay reachable after the loop.
require_order("${route}" "//Cycle over the combinations of these 5 nodes." "AI_VALUE2(bool, \"action useful\", \"hearthstone\")" "hearthstone after the loop")
require_order("${route}" "AI_VALUE2(bool, \"action useful\", \"hearthstone\")" "routeNow + sPlayerbotAIConfig.perfRouteFailCooldownSeconds" "cooldown after the hearthstone")
# The start skip waits for a hearthstone fallback that could use the target leg.
require_text("${node_cpp}" "if (route_precheck::Skips(build.level, route_precheck::Kind::Start, build.start) && !PrecheckHearthstoneUseful(bot))" "start skip not while the hearthstone may run")

# 6. getFullPath: the direct check, the deferred attempt only on a level-4 skip, the late answer kept.
function_body("${node_cpp}" "TravelPath TravelNodeMap::getFullPath(WorldPosition startPos, WorldPosition endPos, Unit* unit, ai::path_fail::RouteFailDetail* outDetail)" full)
require_text("${full}" "PrecheckScope precheckScope(sPlayerbotAIConfig.perfRoutePrecheck, startPos, endPos, outDetail);" "scope with the switch")
require_text("${full}" "precheck->directDeferred = route_precheck::Skips(precheck->level, route_precheck::Kind::Direct, precheck->direct);" "deferred only on a skip")
require_text("${full}" "    if (precheck && precheck->directDeferred)\n        beginPath = { startPos };\n    else\n        beginPath = endPos.getPathFromPath({ startPos }, unit, 40);" "direct attempt unchanged unless deferred")
require_order("${full}" "sTravelNodeMap.getRoute(startPos, endPos, beginPath, endPath, unit, outDetail);" "if (precheck && precheck->directLateOk)" "late direct answer after getRoute")
require_order("${full}" "if (precheck && precheck->directLateOk)" "if (route.isEmpty())" "late direct answer before the empty route")
require_text("${node_cpp}" "            startPath = endPos.getPathFromPath({ startPos }, unit, 40);" "late direct attempt as the original")

# 7. Lines: [RoutePrecheck] once a minute when on; [RouteFailDetail] gets the checks only with the switch.
function_body("${rnd_mgr}" "void ReportRoutePrecheck()" report)
require_text("${report}" "if (!sPlayerbotAIConfig.perfRoutePrecheck || now < lastReport + 60)\n        return;" "nothing when off, once a minute")
require_text("${policy}" "\"[RoutePrecheck] checks=%llu hits=%llu wrong=%llu end_nopoly=\"" "minute line with the spec head")
require_text("${policy}" "\" skips_direct=%llu level=%u\"" "level last")
require_text("${move_cpp}" "std::string const suffix = route_time::FormatDetail(d.times) + route_precheck::FormatDetail(d.precheck)" "detail appendix")
require_text("${policy}" "    if (!b.level)\n        return std::string();" "no detail text when off")
function_body("${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd)
require_order("${upd}" "\n    ReportRouteTime();" "\n    ReportRoutePrecheck();" "after [RouteTime]")
require_order("${upd}" "\n    ReportRoutePrecheck();" "sPerformanceMonitor.Init(0, 0);" "report before the PerfMon Init")

# 8. No lock: relaxed atomics, the thread's own Detour query, no shared mutex.
forbid_text("${policy}" "mutex" "a lock in the pre-check")
forbid_text("${policy}" "memory_order_seq_cst" "non-relaxed counters")

message(STATUS "route_precheck source contract passed")
