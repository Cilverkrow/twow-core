# twow-repo#541 (grid sleep, AiPlayerbot.Park.SleepGrid, default off): a parked bot and its pet do not
# activate the map cells around them. Only the cell update is skipped - grid loading and unloading still
# count the bot, so its grid stays loaded. Combat, attackers, death, teleport and taxi wake it at once;
# unpark clears the flag. [ParkSleep] once per minute.
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
file(READ "${CORE_SOURCE_DIR}/src/game/Maps/GridStates.cpp" grid_states)
file(READ "${CORE_SOURCE_DIR}/src/game/Objects/Player.cpp" player_cpp)
file(READ "${CORE_SOURCE_DIR}/src/game/Objects/Player.h" player_h)

# Switch, default off.
require_text("${config_cpp}" "parkSleepGrid = config.GetBoolDefault(\"AiPlayerbot.Park.SleepGrid\", false);" "SleepGrid default off")
require_text("${config_dist}" "\nAiPlayerbot.Park.SleepGrid = 0\n" "SleepGrid = 0 in the dist config")

# Module: the flag only once parked (not while travelling), only by the switch; unpark clears and counts it.
region("${mgr}" "void RandomPlayerbotMgr::ProcessParkedBots()" "void RandomPlayerbotMgr::ParkBindCheck(" process)
region("${process}" "case ai::park::Stage::Parked:" "break;" parked_case)
require_text("${parked_case}" "bot->SetGridSleep(sPlayerbotAIConfig.parkSleepGrid);" "flag follows the switch in the parked stage")
region("${process}" "case ai::park::Stage::Travel:" "case ai::park::Stage::Parked:" before_parked)
forbid_text("${before_parked}" "SetGridSleep(" "sleep flag before the bot has arrived")
region("${mgr}" "bool RandomPlayerbotMgr::ParkBot(" "void RandomPlayerbotMgr::UnparkBot(" park_bot)
forbid_text("${park_bot}" "SetGridSleep(" "sleep flag in ParkBot (travel and teleport still to come)")
region("${mgr}" "void RandomPlayerbotMgr::UnparkBot(" "void RandomPlayerbotMgr::ReportParkSleep()" unpark)
require_text("${unpark}" "++parkSleepUnparkWakes;
        bot->SetGridSleep(false);" "unpark clears the flag and counts the wake")

# Switches handed to the core each pass and the minute line, without a new return in UpdateAIInternal.
region("${mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "sPerformanceMonitor.Init(0, 0);" pre_init)
require_text("${pre_init}" "
    ReportParkSleep();" "ReportParkSleep each pass (an active call, not a comment)")
region("${mgr}" "void RandomPlayerbotMgr::ReportParkSleep()" "\n}" report)
require_order("${report}" "Map::SetGridSleepSwitches(sPlayerbotAIConfig.parkSleepGrid, stats);" "if (!stats || now < lastReport + 60)" "switches before the minute gate")
foreach(field enabled parked flagged region_updates sleeping_per_update woken_per_update cells_marked_per_update wake_attack wake_other wake_unpark)
  require_text("${report}" "${field}=" "[ParkSleep] field ${field}")
endforeach()
require_text("${report}" "[ParkSleep] " "[ParkSleep] line")

# Core: who sleeps. Any fight, death, teleport or taxi flight keeps the cells awake.
region("${player_cpp}" "bool Player::IsGridSleeping() const" "\n}" sleeping)
foreach(cond "HasGridSleepFlag()" "IsAlive()" "!IsInCombat()" "getAttackers().empty()" "!IsBeingTeleported()" "!IsTaxiFlying()")
  require_text("${sleeping}" "${cond}" "sleep condition ${cond}")
endforeach()
require_text("${player_h}" "std::atomic<bool> m_gridSleep{ false };" "flag atomic (world thread writes, map threads read)")

# Core: both cell update paths skip sleeping players and the active objects of sleeping owners.
region("${map_cpp}" "inline void Map::UpdateActiveCellsAsynch(uint32 now, uint32 diff)" "inline void Map::UpdateActiveCellsSynch(" async)
require_order("${async}" "if (gridSleep && SkipSleepingPlayer(plr, tally))" "MarkCellsAroundObject(plr);" "async: sleeping player skipped before marking")
require_order("${async}" "if (gridSleep && SkipSleepingOwnerObject(*this, *m_activeNonPlayersIter))" "MarkCellsAroundObject(*m_activeNonPlayersIter);" "async: pet of a sleeping owner skipped")
region("${map_cpp}" "inline void Map::UpdateActiveCellsSynch(uint32 now, uint32 diff)" "inline void Map::UpdateCells(" sync)
require_order("${sync}" "if (gridSleep && SkipSleepingPlayer(plr, tally))" "UpdateCellsAroundObject(now, diff, plr);" "sync: sleeping player skipped")
require_order("${sync}" "if (gridSleep && SkipSleepingOwnerObject(*this, obj))" "UpdateCellsAroundObject(now, diff, obj);" "sync: pet of a sleeping owner skipped")
region("${map_cpp}" "bool SkipSleepingOwnerObject(Map& map, WorldObject const* object)" "\n}" owner_skip)
require_text("${owner_skip}" "unit->IsInCombat()" "a fighting pet stays awake")

# Negative: grid loading and unloading never look at the sleep flag (a grid with a player must stay loaded).
region("${map_cpp}" "bool Map::ActiveObjectsNearGrid(uint32 x, uint32 y) const" "\n}" near_grid)
forbid_text("${near_grid}" "GridSleep" "sleep flag in ActiveObjectsNearGrid")
forbid_text("${grid_states}" "GridSleep" "sleep flag in the grid states")
region("${map_cpp}" "bool Map::UnloadGrid(" "\n}" unload_grid)
forbid_text("${unload_grid}" "GridSleep" "sleep flag in UnloadGrid")

message(STATUS "PARK_SLEEP_GRID_SOURCE_CONTRACT=PASS")
