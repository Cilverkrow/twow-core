# twow-repo#540: start-phase spread of journeys - jitter on the first travel choice after login,
# long-move budget before MoveTo in the first minutes after a server start, config switches, trace.
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/strategy/actions/MoveToTravelTargetAction.cpp" move)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "choose|!startup_travel::JitterDone(uint64(time(nullptr)), uint64(bot->GetLoginTime()), bot->GetGUIDLow(),"
    "choose|sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())"
    "move|startup_travel::InWindow(sWorld.GetUptime(), sPlayerbotAIConfig.startupTravelWindowSeconds)"
    "move|!startup_travel::SharedBudget().TryTake(nowMs, sPlayerbotAIConfig.startupTravelMaxLongMovesPerSlot)"
    "move|[StartupTravel] uptime=%u deferred_jitter=%u deferred_budget=%u"
    "move|LogStartupTravel();"
    "config_cpp|AiPlayerbot.StartupTravel.JitterSeconds\", 300"
    "config_cpp|AiPlayerbot.StartupTravel.WindowSeconds\", 900"
    "config_cpp|AiPlayerbot.StartupTravel.MaxLongMovesPerSlot\", 4"
    "config_dist|AiPlayerbot.StartupTravel.JitterSeconds = 300"
    "config_dist|AiPlayerbot.StartupTravel.WindowSeconds = 900"
    "config_dist|AiPlayerbot.StartupTravel.MaxLongMovesPerSlot = 4")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "startup travel: missing ${needle}")
  endif()
endforeach()
# The budget decides before the move is built, and a deferral is no failed move (no IncRetry).
string(FIND "${move}" "startup_travel::SharedCounters().budget.fetch_add(1, std::memory_order_relaxed);" deferred)
string(FIND "${move}" "bool canMove = MoveTo(mapId, x, y, z, false, false);" moveto)
string(FIND "${move}" "target->IncRetry(true);" retry)
if (deferred EQUAL -1 OR moveto EQUAL -1 OR NOT deferred LESS moveto OR NOT moveto LESS retry)
  message(FATAL_ERROR "startup travel: the long-move budget must defer before MoveTo, outside the retry path")
endif()
message(STATUS "STARTUP_TRAVEL_CONTRACT=PASS")
