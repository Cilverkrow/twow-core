cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet D65 (AiPlayerbot.Diet.HunterAspectGate, default 0): the vanilla "aspect of the hawk"
# trigger asks for the known spell first. With 0 the trigger is the plain BuffTrigger as before. Gate only: no
# monkey fallback, the node keeps no alternative.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterTriggers.h" hunter_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterStrategy.cpp" hunter_strategy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" playerbot_ai_cpp)

# Switch: default 0 in member, loader and dist config.
require_text("${config_h}" "bool dietHunterAspectGate = false;" "dietHunterAspectGate member default false")
require_text("${config_cpp}" "dietHunterAspectGate = config.GetBoolDefault(\"AiPlayerbot.Diet.HunterAspectGate\", false);" "HunterAspectGate read with default 0")
require_text("${config_dist}" "\nAiPlayerbot.Diet.HunterAspectGate = 0\n" "HunterAspectGate = 0 in the dist config")

# Trigger: vanilla-only override, spell check first behind the switch, then the old BuffTrigger answer.
region("${hunter_triggers_h}" "class AspectOfTheHawkTrigger : public BuffTrigger" "\n    };" hawk_trigger)
require_text("${hawk_trigger}" "AspectOfTheHawkTrigger(PlayerbotAI* ai) : BuffTrigger(ai, \"aspect of the hawk\") {}" "Hawk trigger constructor unchanged")
require_text("${hawk_trigger}" "#ifdef MANGOSBOT_ZERO" "D65 override only in the vanilla build")
require_text("${hawk_trigger}" "if (sPlayerbotAIConfig.dietHunterAspectGate && !AI_VALUE2(uint32, \"spell id\", spell))\n                return false;" "D65 known-spell gate behind the switch")
require_text("${hawk_trigger}" "return BuffTrigger::IsActive();" "D65 falls through to the old BuffTrigger check")
require_order("${hawk_trigger}" "dietHunterAspectGate" "return BuffTrigger::IsActive();" "D65 gate before the aura check")
require_order("${hawk_trigger}" "#ifdef MANGOSBOT_ZERO" "virtual bool IsActive() override" "D65 override inside the vanilla block")
require_order("${hawk_trigger}" "virtual bool IsActive() override" "#endif" "D65 override closed by #endif")
forbid_text("${hawk_trigger}" "monkey" "D65 adds no monkey fallback")

# Neutrality anchors: the node has no alternative factory entry, and the cast resolves the same "spell id" value.
region("${hunter_strategy}" "class HunterStrategyActionNodeFactory" "\n};" node_factory)
forbid_text("${node_factory}" "creators[\"aspect of the hawk\"]" "an alternative node for aspect of the hawk")
region("${hunter_strategy}" "#ifdef MANGOSBOT_ZERO" "#endif" hunter_zero)
require_text("${hunter_zero}" "NextAction::array(0, new NextAction(\"aspect of the hawk\", ACTION_HIGH + 5), NULL)" "combat Hawk node unchanged")
require_text("${hunter_zero}" "NextAction::array(0, new NextAction(\"aspect of the hawk\", ACTION_NORMAL), NULL)" "non-combat Hawk node unchanged")
require_text("${playerbot_ai_cpp}" "return CanCastSpell(aiObjectContext->GetValue<uint32>(\"spell id\", name)->Get()" "CanCastSpell(name) resolves the same spell id value")

message(STATUS "diet D65 hunter aspect gate source contract passed")
