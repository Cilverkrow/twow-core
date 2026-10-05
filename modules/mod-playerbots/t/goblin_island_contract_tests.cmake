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
# Hotfix 8.29a: the island check is gated by race/map/box before any terrain lookup.
string(FIND "${ai_cpp}" "bool const islandCandidate = gi::MayBeOnIsland(bot->getRace(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());" gate)
string(FIND "${ai_cpp}" "if (islandCandidate && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())" gated)
string(FIND "${ai_cpp}" "gi::ShouldLeaveIsland(bot->GetZoneId()" zone)
if (gate EQUAL -1 OR gated EQUAL -1 OR zone LESS gate)
  message(FATAL_ERROR "goblin island: the zone lookup must be gated by MayBeOnIsland (8.29a)")
endif()
message(STATUS "GOBLIN_ISLAND_CONTRACT=PASS")
