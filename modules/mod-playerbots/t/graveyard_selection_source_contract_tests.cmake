function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/values/DeadValues.cpp" dead_values)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ReviveFromCorpseAction.cpp" revive)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #276: both non-nearest graveyard branches are distance bounded.
require_text("${dead_values}" "graveyard_policy::IsZoneLevelAppropriate(" "alternate graveyard zone level gate")
require_text("${dead_values}" "graveyard_policy::IsWithinAlternateDistance(sqrt(dist), sPlayerbotAIConfig.maxAlternateGraveyardDistance)" "alternate graveyard distance bound")
require_text("${dead_values}" "graveyard_policy::IsWithinAlternateDistance(AI_VALUE2(GuidPosition, \"graveyard\", \"travel\").fDist(corpse), sPlayerbotAIConfig.maxAlternateGraveyardDistance)" "travel graveyard distance bound")
require_text("${dead_values}" "return AI_VALUE2(GuidPosition, \"graveyard\", \"self\");" "nearest graveyard remains the fallback")
require_text("${revive}" "sLog.outBasic(\"[BOT GRAVEYARD] event=teleport" "visible ghost teleport diagnostics")
require_text("${revive}" "sLog.outBasic(\"[BOT GRAVEYARD] event=revive" "visible spirit healer revive diagnostics")
require_text("${config_template}" "AiPlayerbot.MaxAlternateGraveyardDistance = 2500" "documented default bound")
