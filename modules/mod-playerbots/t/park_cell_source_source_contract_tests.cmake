# twow-repo#541 P2 D0 (AiPlayerbot.Park.CellSourceStats, default off, diagnostics only): which source keeps the
# continent cells awake. With the switch the players are marked in two passes (real clients, then bots) and the marked
# cells are counted after each source; without it the single legacy pass over all players. [CellSource] once a minute.
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

# Switch, default off.
require_text("${config_cpp}" "parkCellSourceStats = config.GetBoolDefault(\"AiPlayerbot.Park.CellSourceStats\", false);" "CellSourceStats default off")
require_text("${config_dist}" "\nAiPlayerbot.Park.CellSourceStats = 0\n" "CellSourceStats = 0 in the dist config")

# Core: the pass split only with the switch, and only on continents; without it one pass (2) over all players.
region("${map_cpp}" "bool InCellPass(Player const* player, int pass)" "\n}" in_pass)
require_text("${in_pass}" "pass == 2 ||" "pass 2 takes every player")
require_text("${in_pass}" "player->HasClientSocket() == (pass == 0)" "pass 0 = real clients, pass 1 = bots")
foreach(path "inline void Map::UpdateActiveCellsAsynch(uint32 now, uint32 diff)|inline void Map::UpdateActiveCellsSynch("
             "inline void Map::UpdateActiveCellsSynch(uint32 now, uint32 diff)|inline void Map::UpdateCells(")
  string(REPLACE "|" ";" bounds "${path}")
  list(GET bounds 0 first)
  list(GET bounds 1 last)
  region("${map_cpp}" "${first}" "${last}" body)
  require_text("${body}" "bool const sourceStats = s_cellSourceStats.load(std::memory_order_relaxed) && IsContinent();" "switch read per update (${first})")
  require_text("${body}" "for (int pass = sourceStats ? 0 : 2, lastPass = sourceStats ? 1 : 2; pass <= lastPass; ++pass)" "single pass without the switch (${first})")
  require_order("${body}" "if (!InCellPass(plr, pass))" "if (gridSleep && SkipSleepingPlayer(plr, tally))" "pass filter before the sleep check, each player once (${first})")
  require_text("${body}" "if (sourceStats)
        AddCellSourceTally(sourceCount, marked_cells.count(), m_activeNonPlayers.size(), GetGridActivationDistance());" "tally only with the switch (${first})")
endforeach()

# Negative: the cell visit itself (threads, safe distance) is untouched by the diagnostics.
region("${map_cpp}" "inline void Map::UpdateActiveCellsCallback(" "\n}" callback)
forbid_text("${callback}" "CellSource" "cell-source counting in the cell visit")
forbid_text("${callback}" "sourceStats" "cell-source switch in the cell visit")

# Module: the switch is handed to the core each pass; the minute line with its fields.
region("${mgr}" "void RandomPlayerbotMgr::ReportParkSleep()" "static uint32 lastReport = 0;" report)
require_text("${report}" "Map::SetCellSourceStats(sPlayerbotAIConfig.parkCellSourceStats);" "switch handed to the core")
require_order("${report}" "Map::SetCellSourceStats(sPlayerbotAIConfig.parkCellSourceStats);" "if (sPlayerbotAIConfig.parkCellSourceStats && now >= lastSourceReport + 60)" "switch before the minute gate")
foreach(field region_updates cells_total cells_real cells_bot_only cells_npc_only real_players bots active_npcs activation)
  require_text("${report}" "${field}=" "[CellSource] field ${field}")
endforeach()
require_text("${report}" "[CellSource] " "[CellSource] line")

message(STATUS "PARK_CELL_SOURCE_SOURCE_CONTRACT=PASS")
