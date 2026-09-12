if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "PB_MODULE_DIR is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

function(require_absent text needle label)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Unexpected ${label}: ${needle}")
  endif()
endfunction()

file(READ "${PB_MODULE_DIR}/src/playerbot/PlayerbotAIConfig.cpp" config)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/values/TravelValues.cpp" travel_values)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/values/QuestValues.cpp" quest_values)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/ChooseTravelTargetAction.cpp" quest_travel)

foreach(required
  "AiPlayerbot.QuestFirstProgression.Enabled"
  "AiPlayerbot.QuestFirstProgression.AutonomousLogSoftLimit"
  "AiPlayerbot.QuestFirstProgression.RejectBelowLevelDelta"
  "AiPlayerbot.QuestFirstProgression.RetireBelowLevelDelta")
  require_text("${config}" "${required}" "configuration key")
endforeach()

require_text("${travel_values}" "TravelDestinationPurpose::Grind" "generic-grind hook")
require_text("${travel_values}" "UsesQuestFirstProgression(bot)" "roster-only grind gate")
require_text("${quest_values}" "ActiveQuestSlotCount(bot)" "soft-limit gate")
require_text("${quest_values}" "questFirstProgressionRejectBelowLevelDelta" "stale-offer boundary")
require_text("${quest_values}" "(int32)level >= quest->GetQuestLevel()" "delta-four reject boundary")
require_text("${quest_travel}" "RetireOneStaleQuest(PlayerbotAI* ai, focusQuestTravelList const& focusList)" "explicit retirement value parameter")
require_text("${quest_travel}" "RetireOneStaleQuest(ai, focusList)" "context-owned retirement value handoff")
require_text("${quest_travel}" "bot->RemoveQuestAtSlot(slot)" "normal core removal API")
require_text("${quest_travel}" "Item and source-item quests are protected" "shared-item protection")
require_text("${quest_travel}" "finished > 0" "turn-in-first ordering")

string(FIND "${quest_travel}" "bool RetireOneStaleQuest(" retire_begin)
string(FIND "${quest_travel}" "inline std::string GetTravelPurposeName" retire_end)
if(retire_begin EQUAL -1 OR retire_end EQUAL -1 OR retire_end LESS retire_begin)
  message(FATAL_ERROR "Cannot isolate RetireOneStaleQuest helper")
endif()
math(EXPR retire_length "${retire_end} - ${retire_begin}")
string(SUBSTRING "${quest_travel}" ${retire_begin} ${retire_length} retire_helper)
require_absent("${retire_helper}" "AI_VALUE(" "action-context lookup in free retirement helper")
message(STATUS "QUEST_FIRST_PROGRESSION_SOURCE_CONTRACT=PASS")
