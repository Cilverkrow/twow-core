cmake_policy(VERSION 3.16)

if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#541 part C (owner 09.10.2026: "würde das mit rein nehmen"): Continents.Layout 0 keeps the legacy
# region polygons; 14/16/18/20 take the region from generated cell tables (20: owner decision 10.10.2026, every
# capital its own region, ids 1-12 and 21-32). A fight in a border cell keeps the
# region; [RegionSwitch] counts switches. Dynamic split/merge at runtime is out of scope (owner 09.10.).

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if (first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if (length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("src/game/World.cpp" world)
read_source("src/mangosd/mangosd.conf.dist.in" conf)
read_source("src/game/Maps/MapManager.cpp" mapmgr)
read_source("src/game/Objects/Player.cpp" player)
read_source("src/game/Maps/ContinentRegionTables.cpp" tables)
read_source("src/game/Maps/ContinentRegionTables.h" tables_h)
read_source("src/game/Maps/MapPersistentStateMgr.cpp" persistent)
read_source("tools/continent_regions/gen_continent_regions.py" generator)
read_source("t/continent_layout_541_test.cpp" table_test)
read_source("src/game/CMakeLists.txt" game_cmake)

# Switch, default 0 (legacy polygons), documented (layout 20 included).
require_text("${world}" "setConfig(CONFIG_UINT32_CONTINENTS_LAYOUT, \"Continents.Layout\", 0);" "Continents.Layout default 0")
require_text("${conf}" "\nContinents.Layout = 0\n" "Continents.Layout = 0 in the dist config")
require_text("${conf}" "#   20 = cell table, 12 + 12 regions" "Continents.Layout 20 documented")
require_text("${conf}" "ids 1-12 and 21-32" "layout 20 region ids documented")
require_text("${game_cmake}" "Maps/ContinentRegionTables.cpp" "generated tables in the game library")

# Generator: layout 20 of the owner decision (10.10.2026), per-layout first ids, rectangles before the BFS fill.
require_text("${generator}" "LAYOUT20 = {" "layout 20 zone lists")
require_text("${generator}" "20: {mp: [(n, list(z)) for n, z in r] for mp, r in LAYOUT20.items()}" "layout 20 generated")
require_text("${generator}" "FIRST_ID = {14: {0: 1, 1: 11}, 16: {0: 1, 1: 11}, 18: {0: 1, 1: 11}, 20: {0: 1, 1: 21}}" "first region id per layout")
require_text("${generator}" "(2, -5060.0, -4540.0, -1360.0, -840.0, \"Ironforge\")" "Ironforge rectangle")
require_text("${generator}" "(3, 1180.0, 1880.0, -20.0, 520.0, \"Undercity + Ruins of Lordaeron\")" "Undercity rectangle")
region("${generator}" "def build(lid, layout, lab):" "def cell_of(" build)
require_order("${build}" "votes[cx][cy] = c.most_common(1)[0][0]" "for rid, x0, x1, y0, y1, _ in RECTS.get(lid, {}).get(mp, []):" "rectangles after the zone vote")
require_order("${build}" "for rid, x0, x1, y0, y1, _ in RECTS.get(lid, {}).get(mp, []):" "queue = deque(" "rectangles before the BFS fill")
require_text("${generator}" "KNOWN_REGION = {20: KNOWN20}" "known places of layout 20")

# Lookup: the table only with a layout, before the legacy polygons; transition from the table.
region("${mapmgr}" "uint32 MapManager::GetContinentInstanceId(" "void MapManager::ScheduleFarTeleport(" lookup)
require_order("${lookup}" "if (!sWorld.getConfig(CONFIG_BOOL_CONTINENTS_INSTANCIATE))" "if (active.layout != 0 && mapId <= 1)" "instancing switch first")
require_order("${lookup}" "if (active.layout != 0 && mapId <= 1)" "const static float topNorthSouthLimit[]" "table before the polygons")
require_text("${lookup}" "*transitionArea = (cell & continent_regions::kTransitionBit) != 0;" "border cells from the table")
require_text("${lookup}" "return MAP0_SOUTH;" "legacy polygons kept for layout 0")

# Read once (thread-safe static), invalid layouts fall back to 0. Per-layout ids: first and last from the table,
# every id between them present, within 1..min(127, RESERVED_INSTANCES_LAST - 1), the continents' ranges apart.
require_text("${tables_h}" "inline std::uint32_t FirstRegion(std::vector<std::uint8_t> const& cells)" "FirstRegion next to LastRegion")
require_text("${tables_h}" "inline bool RegionsContiguous(std::vector<std::uint8_t> const& cells, std::uint32_t first, std::uint32_t last)" "contiguity helper")
require_text("${mapmgr}" "static ActiveContinentLayout const active = BuildContinentLayout();" "decoded once")
require_text("${mapmgr}" "using the polygons (0)" "fallback to the polygons")
region("${mapmgr}" "struct ActiveContinentLayout" "ActiveContinentLayout const& GetActiveContinentLayout()" build_layout)
require_text("${build_layout}" "uint32 first[2] = { MAP0_FIRST, MAP1_FIRST };" "layout 0 first ids")
require_text("${build_layout}" "uint32 last[2] = { MAP0_SOUTH, MAP1_SOUTH };" "layout 0 last ids")
require_text("${build_layout}" "std::min<uint32>(continent_regions::kRegionMask, RESERVED_INSTANCES_LAST - 1)" "region ids below the border bit and the generated instance ids")
require_text("${build_layout}" "built.first[mapId] = continent_regions::FirstRegion(built.cells[mapId]);" "first id from the table")
require_text("${build_layout}" "built.last[mapId] = continent_regions::LastRegion(built.cells[mapId]);" "last id from the table")
require_text("${build_layout}" "!continent_regions::RegionsContiguous(built.cells[mapId], built.first[mapId], built.last[mapId])" "contiguity validated")
require_text("${build_layout}" "if (!(built.last[0] < built.first[1] || built.last[1] < built.first[0]))" "ranges must not overlap")
require_order("${build_layout}" "!continent_regions::RegionsContiguous(" "return built;" "validation before use")
if (build_layout MATCHES "lastAllowed|MAP0_LAST|MAP1_LAST")
  message(FATAL_ERROR "Fixed MAP0_LAST/MAP1_LAST bounds in the layout validation (layout 20 uses ids 21-32)")
endif()
region("${mapmgr}" "uint32 MapManager::GetContinentFirstRegion(" "void MapManager::ReportRegionSwitches()" bounds)
require_text("${bounds}" "return GetActiveContinentLayout().first[mapId == 0 ? 0 : 1];" "first region of the active layout")
require_text("${bounds}" "return GetActiveContinentLayout().last[mapId == 0 ? 0 : 1];" "last region of the active layout")

# Map creation follows the layout's region range.
region("${mapmgr}" "void MapManager::GetOrCreateContinentInstances(" "Map* MapManager::CreateMap(" create)
require_text("${create}" "for (uint32 i = GetContinentFirstRegion(0); i <= GetContinentLastRegion(0); ++i)" "Eastern Kingdoms regions per layout")
require_text("${create}" "for (uint32 i = GetContinentFirstRegion(1); i <= GetContinentLastRegion(1); ++i)" "Kalimdor regions per layout")

# Respawn loaders: the manager's range, last id included (the old MAP0_LAST/MAP1_LAST loop with '<' left out
# region 20 of layout 18). The creature loader keeps its Continents.Instanciate gate, the gameobject loader none.
region("${persistent}" "void MapPersistentStateManager::LoadCreatureRespawnTimes()" "void MapPersistentStateManager::LoadGameobjectRespawnTimes()" creature_respawn)
string(FIND "${persistent}" "void MapPersistentStateManager::LoadGameobjectRespawnTimes()" go_start)
if (go_start EQUAL -1)
  message(FATAL_ERROR "Missing LoadGameobjectRespawnTimes")
endif()
string(SUBSTRING "${persistent}" ${go_start} -1 go_rest)
string(FIND "${go_rest}" "\n}\n" go_end)
string(SUBSTRING "${go_rest}" 0 ${go_end} go_respawn)
foreach(loader creature_respawn go_respawn)
  set(body "${${loader}}")
  if (body MATCHES "MAP0_FIRST|MAP0_LAST|MAP1_FIRST|MAP1_LAST")
    message(FATAL_ERROR "${loader}: fixed MAP0/MAP1 FIRST/LAST bounds (must follow the active layout)")
  endif()
  foreach(map 0 1)
    require_text("${body}" "beginInstance = int(sMapMgr.GetContinentFirstRegion(${map}));" "${loader}: first region of map ${map}")
    require_text("${body}" "endInstance = int(sMapMgr.GetContinentLastRegion(${map})) + 1;" "${loader}: last region of map ${map} included")
  endforeach()
  require_text("${body}" "for (int instance = beginInstance; instance < endInstance; ++instance)" "${loader}: loop up to endInstance")
endforeach()
require_order("${creature_respawn}" "if (sWorld.getConfig(CONFIG_BOOL_CONTINENTS_INSTANCIATE))" "beginInstance = int(sMapMgr.GetContinentFirstRegion(0));" "creature loader keeps its instancing gate")
string(FIND "${go_respawn}" "CONFIG_BOOL_CONTINENTS_INSTANCIATE" go_gate)
if (NOT go_gate EQUAL -1)
  message(FATAL_ERROR "gameobject respawn loader gained an instancing gate (behaviour must stay as before)")
endif()

# A fight in a border cell defers the switch (counted); switches counted; [RegionSwitch] line.
require_text("${player}" "if (!transition || !IsInCombat())
                sMapMgr.ScheduleInstanceSwitch(this, newInstanceId);
            else
                sMapMgr.CountRegionSwitchDeferred();" "deferred switch in a border fight")
require_text("${mapmgr}" "player->SwitchInstance(it->second))
                ++m_regionSwitches;" "switches counted")
require_text("${mapmgr}" "[RegionSwitch] layout=%u switches=%llu deferred_combat_updates=%llu" "[RegionSwitch] line")

# Table test: layout 20 ranges and places (rectangles, outside the Ironforge gate, Brill).
require_text("${table_test}" "{ 20, { 1, 21 }, { 12, 32 } }," "layout 20 first/last ids in the table test")
require_text("${table_test}" "RegionsContiguous(cells, e.first[mapId], e.last[mapId])" "contiguity in the table test")
require_text("${table_test}" "{ 20, 0, -4838.0f, -1186.0f, 2," "Ironforge in the table test")
require_text("${table_test}" "{ 20, 0, 1595.0f, 231.0f, 3," "Undercity in the table test")
require_text("${table_test}" "{ 20, 0, -5100.0f, -800.0f, 6," "outside the Ironforge gate in the table test")
require_text("${table_test}" "{ 20, 0, 2269.0f, 244.0f, 7," "Brill in the table test")

# Generated by the checked-in tool for all four layouts on both continents. Kept last: the layout 20 tables
# appear only after the generator has run (build slot); every check above is independent of the regeneration.
require_text("${tables}" "// Generated by tools/continent_regions/gen_continent_regions.py - do not edit." "generator header")
foreach(layout 14 16 18 20)
  foreach(map 0 1)
    require_text("${tables}" "{ ${layout}, ${map}, kLayout${layout}Map${map}," "table for layout ${layout} map ${map} (regenerate with tools/continent_regions/gen_continent_regions.py)")
  endforeach()
endforeach()

message(STATUS "CONTINENT_LAYOUT_541_CONTRACT=PASS")
