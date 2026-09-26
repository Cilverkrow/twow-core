function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_source)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #307: every travel destination, not only completed turn-ins.
require_text("${ai_source}" "travelTarget->OnDeathAtDestination();" "death counted against any destination")
require_text("${travel_mgr}" "destination_death::RecordDeath(destinationDeaths[tDestination]" "per-bot destination death record")
require_text("${travel_mgr}" "IsProgressAwareTurnIn())" "turn-ins keep their own rule")
require_text("${travel_mgr}" "[DestinationDeath] state=death_suppressed" "visible suppression")
require_text("${choose}" "persistentTarget->IsDestinationDeathSuppressed(destination)" "suppressed destinations skipped for every purpose")
require_text("${config_source}" "\"AiPlayerbot.DestinationDeaths.Max\", 2" "configurable threshold")
require_text("${config_template}" "AiPlayerbot.DestinationDeaths.CooldownSeconds = 3600" "documented cooldown")

# Real area (sub-zone) level for route danger (Lakeshire 15, not Redridge 0).
require_text("${choose}" "int32 const areaLevel = std::max<int32>(0, position->getAreaLevel());" "route danger uses the real area level")

# #307 (b): fishing spots are level-checked, INVALID_HEIGHT rows skipped, and
# the stored spot is dropped on death (Carler: L7 at Blackwood Lake, area 58).
file(READ "${PB_SOURCE_DIR}/strategy/actions/FishAction.cpp" fish)
require_text("${fish}" "homebind::IsZoneClearlyAboveLevel(uint32(std::max<int32>(0, spot.getAreaLevelOrParent())), bot->GetLevel())" "fishing spot level check (parent zone fallback)")
require_text("${fish}" "spot.getZ() <= INVALID_HEIGHT" "invalid generated spots skipped")
require_text("${ai_source}" "RESET_AI_VALUE2(WorldPosition, \"custom position\", \"fish spot\");" "fishing spot dropped on death")
