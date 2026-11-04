function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #335: quest givers/objectives use the quest tolerance, not the grind gate.
require_text("${travel_mgr}" "quest_area_level::IsQuestLocationLevelValid(questAreaLevel, info.GetLevel()" "quest-only area level rule")
require_text("${travel_mgr}" "!(purposeFlag & ~(questPurposes | (uint32)TravelDestinationPurpose::QuestTaker))" "only pure quest purposes")
require_text("${travel_mgr}" "if (!areaLevel || (uint32)botLevel < areaLevel)" "grind gate unchanged for everything else")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.QuestAreaLevelMargin\", 5" "configurable margin")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.QuestAreaLevelMargin = 5" "documented default")
