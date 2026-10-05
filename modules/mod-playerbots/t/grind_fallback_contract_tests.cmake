# Hotfix 8.32 (twow-repo#544): a quest-first bot grinds after repeated empty quest route searches
# instead of idling; switch, default off, trace.
file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" values)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "values|return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures) &&"
    "choose|[QuestSearch] state=grind_fallback bot=%u level=%u failures=%d"
    "config_cpp|AiPlayerbot.QuestFirstProgression.GrindFallbackFailures\", 0"
    "config_dist|AiPlayerbot.QuestFirstProgression.GrindFallbackFailures = 0")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "grind fallback: missing ${needle}")
  endif()
endforeach()
# The quest-first branch of NeedTravelPurposeValue (Grind) must no longer return a plain false.
string(FIND "${values}" "if (UsesQuestFirstProgression(bot))\n            return false;\n\n        uint32 rpgPhase" old_block)
if (NOT old_block EQUAL -1)
  message(FATAL_ERROR "grind fallback: quest-first bots still never grind")
endif()
message(STATUS "GRIND_FALLBACK_CONTRACT=PASS")
