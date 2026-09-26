function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/DangerMapPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_source)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #307 shared danger map: every access is locked (map update thread pools).
require_text("${policy}" "std::unique_lock lock(mutex);" "locked writes")
require_text("${policy}" "std::shared_lock lock(mutex);" "shared reads")
require_text("${policy}" "MaxCells = 4096" "bounded cell count")

# Source: all bots, never real players, no instances, only deaths to a creature.
require_text("${ai_source}" "sPlayerbotAIConfig.dangerMapEnabled && !IsRealPlayer() && !bot->GetMap()->IsDungeon()" "bot-only open-world recording")
require_text("${ai_source}" "killer->GetTypeId() != TYPEID_UNIT" "deaths to creatures only")
require_text("${ai_source}" "[DangerMap] cells=" "visible map summary")

# Effect: roster bots on their own only, any destination purpose.
require_text("${choose}" "UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster()" "roster-only effect")
require_text("${choose}" "danger_map::Instance().Query(" "destination and route query")
require_text("${choose}" "reason=route_danger detail=death_cluster" "visible deferral")

# Switch off by default; every parameter configurable.
require_text("${config_source}" "\"AiPlayerbot.DangerMap.Enabled\", false" "default off")
foreach(key CellSize WindowSeconds MinDeaths LevelMargin LineSamples)
  require_text("${config_source}" "\"AiPlayerbot.DangerMap.${key}\"" "configurable ${key}")
  require_text("${config_template}" "AiPlayerbot.DangerMap.${key} = " "documented ${key}")
endforeach()
require_text("${config_template}" "AiPlayerbot.DangerMap.Enabled = 0" "dist default off")
