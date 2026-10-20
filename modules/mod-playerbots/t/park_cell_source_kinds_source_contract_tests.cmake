# twow-repo#541 P2 D5 (AiPlayerbot.Park.CellSourceKinds, default off, diagnostics only, needs CellSourceStats): which kind of
# active non-player keeps the npc-only continent cells awake. On 1 of 64 continent cell updates every active non-player is
# classified and the cells it marks first are counted (read-only test before its own marking); [CellSourceKinds] once a
# minute, [CellSourceTop] every 10 minutes. The cell marking and the cell visit themselves stay unchanged.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "CORE_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(READ "${CORE_SOURCE_DIR}/src/game/Maps/Map.cpp" map_cpp)
file(READ "${CORE_SOURCE_DIR}/src/game/Maps/Map.h" map_h)

# Switch, default off.
require_text("${config_cpp}" "parkCellSourceKinds = config.GetBoolDefault(\"AiPlayerbot.Park.CellSourceKinds\", false);" "CellSourceKinds default off")
require_text("${config_dist}" "\nAiPlayerbot.Park.CellSourceKinds = 0\n" "CellSourceKinds = 0 in the dist config")

# Sampling: only with the D0 pass split (sourceStats) and the switch, 1 of 64 updates.
region("${map_cpp}" "bool CellSourceKindSample(Map const& map, bool sourceStats, bool& top)" "\n}" sample)
require_text("${sample}" "if (!sourceStats || !s_cellSourceKinds.load(std::memory_order_relaxed) ||" "sample needs sourceStats and the switch")
require_text("${map_cpp}" "uint32 constexpr CELL_SOURCE_KIND_SAMPLE = 64;" "sample rate 1 of 64")

# The count only reads the marks (same area as MarkCellsAroundObject), it never marks.
region("${map_cpp}" "uint32 CountUnmarkedCellsAround(Map& map, WorldObject const* object)" "\n}" count)
require_text("${count}" "Cell::CalculateCellArea(object->GetPositionX(), object->GetPositionY(), object->GetGridActivationDistance());" "same activation area as the marking")
require_text("${count}" "if (!map.isCellMarked(" "read-only test of the mark")
forbid_text("${count}" "markCell(" "marking inside the count")

# Both update paths: classify + count right before the object's own marking, tally after the D0 tally. (No ";" inside the
# bounds: CMake would split the list there.)
foreach(path "inline void Map::UpdateActiveCellsAsynch(uint32 now, uint32 diff)|inline void Map::UpdateActiveCellsSynch(|MarkCellsAroundObject(*m_activeNonPlayersIter)|TallyCellSourceKind(*this, *m_activeNonPlayersIter, kindTally, cellSourceTop)"
             "inline void Map::UpdateActiveCellsSynch(uint32 now, uint32 diff)|inline void Map::UpdateCells(|UpdateCellsAroundObject(now, diff, obj)|TallyCellSourceKind(*this, obj, kindTally, cellSourceTop)")
  string(REPLACE "|" ";" bounds "${path}")
  list(GET bounds 0 first)
  list(GET bounds 1 last)
  list(GET bounds 2 mark)
  list(GET bounds 3 tally)
  region("${map_cpp}" "${first}" "${last}" body)
  require_text("${body}" "bool const cellSourceKinds = CellSourceKindSample(*this, sourceStats, cellSourceTop);" "sample decision per update (${first})")
  require_text("${body}" "if (cellSourceKinds)
            ${tally}" "count only on a sampled update (${first})")
  require_order("${body}" "${tally}" "${mark}" "count before the object marks its cells (${first})")
  require_order("${body}" "if (gridSleep && SkipSleepingOwnerObject(" "${tally}" "a sleeping pet is neither marked nor counted (${first})")
  require_text("${body}" "if (sourceStats)
        AddCellSourceTally(sourceCount, marked_cells.count(), m_activeNonPlayers.size(), GetGridActivationDistance());
    if (cellSourceKinds)
        AddCellSourceKindTally(kindTally);" "kind tally after the D0 tally (${first})")
endforeach()

# Negative: the cell visit itself is untouched.
region("${map_cpp}" "inline void Map::UpdateActiveCellsCallback(" "\n}" callback)
forbid_text("${callback}" "CellSourceKind" "kind counting in the cell visit")

# Kind names in enum order.
require_text("${map_cpp}" "{ \"pet_bot\", \"pet_player\", \"summon\", \"spawn_active\", \"formation\",
        \"aoi\", \"go_spawn_active\", \"go_aoi\", \"transport\", \"dynobj\", \"other\" }" "kind names in enum order")
set(previous "")
foreach(kind PET_BOT PET_PLAYER SUMMON SPAWN_ACTIVE FORMATION AOI GO_SPAWN_ACTIVE GO_AOI TRANSPORT DYNOBJECT OTHER KIND_COUNT)
  require_text("${map_h}" "CELL_SOURCE_${kind}" "enum value CELL_SOURCE_${kind}")
  if(previous)
    require_order("${map_h}" "CELL_SOURCE_${previous}," "CELL_SOURCE_${kind}" "enum order")
  endif()
  set(previous "${kind}")
endforeach()

# Module: the switch handed to the core with CellSourceStats, the two lines with their fields.
region("${mgr}" "void RandomPlayerbotMgr::ReportParkSleep()" "static uint32 lastReport = 0;" report)
require_text("${report}" "Map::SetCellSourceKinds(sPlayerbotAIConfig.parkCellSourceStats && sPlayerbotAIConfig.parkCellSourceKinds);" "switch only with CellSourceStats")
require_text("${report}" "[CellSourceKinds] samples=" "[CellSourceKinds] line")
foreach(field map= instances= objects= cells= inst= rank= kind= entry= guid= x= y= nearest=)
  require_text("${report}" "${field}" "[CellSourceTop] field ${field}")
endforeach()
require_order("${report}" "Map::TakeCellSourceTop();" "Map::RequestCellSourceTop();" "rows of the last request before the next request")

message(STATUS "PARK_CELL_SOURCE_KINDS_SOURCE_CONTRACT=PASS")
