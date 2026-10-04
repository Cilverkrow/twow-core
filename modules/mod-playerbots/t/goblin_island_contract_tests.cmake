# Hotfix 8.29: goblins leave Blackstone Island by the flying machine, followers fly along, rescue to Durotar.
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
foreach (pair
    "ai_cpp|gi::ShouldLeaveIsland(bot->GetZoneId(), bot->GetLevel(), questProgress.IdleSeconds(now))"
    "ai_cpp|bot->ActivateTaxiPathTo(gi::IslandPath, 0, true);"
    "ai_cpp|[Travel] state=island_flight bot=%u"
    "ai_cpp|if (realMaster->IsTaxiFlying())"
    "ai_cpp|[Travel] state=follow_script_taxi bot=%u"
    "mgr|ai::quest_search::goblin_island::UsesGoblinStartRescue(bot->getRace(), level)")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "goblin island: missing ${needle}")
  endif()
endforeach()
# The goblin rescue comes before the random start-area loop.
string(FIND "${mgr}" "UsesGoblinStartRescue(bot->getRace(), level)" goblin)
string(FIND "${mgr}" "PlayerInfo const* info = firstInfo(race);" loop)
if (goblin GREATER loop)
  message(FATAL_ERROR "goblin island: the Durotar rescue must come before the start-area loop")
endif()
message(STATUS "GOBLIN_ISLAND_CONTRACT=PASS")
