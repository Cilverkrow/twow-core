function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #329 step 2: roster quest objectives travel on observed progress, not on the
# generic travel time budget that every fight on the way used up.
require_text("${travel_mgr}" "statusTime = IsProgressAwareQuestTravel() ? 0 :" "no generic travel budget for progress-aware quest travel")
require_text("${travel_mgr}" "dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination)" "objective destinations included")
require_text("${travel_mgr}" "bot->GetQuestStatus(objective->GetQuestId()) == QUEST_STATUS_INCOMPLETE" "only incomplete objectives")
require_text("${travel_mgr}" "if (IsProgressAwareQuestTravel())" "stall recovery for objectives")
require_text("${travel_mgr}" "QuestTravelDestination const* destination = dynamic_cast<QuestTravelDestination const*>(tDestination);" "shared progress observation")
require_text("${travel_mgr}" "\"status_time_exceeded_travel\"" "travel budget drops distinguishable")
require_text("${travel_mgr}" "\"status_time_exceeded_work\"" "work phase drops distinguishable")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.ProgressAwareObjectives\", true" "rollback switch, default on")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.ProgressAwareObjectives = 1" "documented default")
