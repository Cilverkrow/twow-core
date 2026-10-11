cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g2 (priest, mage, warlock). Every fix sits behind its own switch
# AiPlayerbot.Diet.<Key>, default 0; with 0 the old trigger/strategy path runs unchanged.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "CORE_SOURCE_DIR is required")
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

# A node registered only under a strategy-level gate (4-space body, 8-space push), and never ungated.
function(require_gated_node text gate trigger action relevance description)
  set(gated "    if (${gate})\n    {\n        triggers.push_back(new TriggerNode(\n            \"${trigger}\",\n            NextAction::array(0, new NextAction(\"${action}\", ${relevance}), NULL)));\n    }")
  require_text("${text}" "${gated}" "${description}: gated node (switch off keeps it)")
  set(ungated "\n    triggers.push_back(new TriggerNode(\n        \"${trigger}\",\n        NextAction::array(0, new NextAction(\"${action}\"")
  forbid_text("${text}" "${ungated}" "${description}: ungated node")
endfunction()

# A trigger class whose IsActive first asks the switch, then runs the old base IsActive unchanged.
function(require_switch_first_trigger text clazz base spell switch_member description)
  region("${text}" "class ${clazz} : public ${base}" "};" body)
  require_text("${body}" "${clazz}(PlayerbotAI* ai) : ${base}(ai, \"${spell}\")" "${description}: same spell name")
  require_text("${body}" "if (sPlayerbotAIConfig.${switch_member} && !AI_VALUE2(uint32, \"spell id\", spell))\n                return false;\n            return ${base}::IsActive();" "${description}: switch + spell id gate, then the old IsActive")
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(READ "${CORE_SOURCE_DIR}/src/game/SharedDefines.h" shared_defines)

# Every switch defaults to 0.
foreach(pair
    "dietPriestRacials|PriestRacials")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 member)
  list(GET parts 1 key)
  require_text("${config_h}" "bool ${member} = false;" "${member} member default false")
  require_text("${config_cpp}" "${member} = config.GetBoolDefault(\"AiPlayerbot.Diet.${key}\", false);" "${key} read with default 0")
  require_text("${config_dist}" "\nAiPlayerbot.Diet.${key} = 0\n" "${key} = 0 in the dist config")
endforeach()

# ---------------------------------------------------------------------------------------------------------
# D20 (AiPlayerbot.Diet.PriestRacials): priest race spells only for races that can learn them.
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestStrategy.h" priest_strategy_h)
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestStrategy.cpp" priest_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/priest/ShadowPriestStrategy.cpp" shadow_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestTriggers.h" priest_triggers_h)

# Race masks = 1 << (race - 1) for the core's race ids (skill_line_ability.race_mask 16/128/8/5).
foreach(race "RACE_HUMAN|1" "RACE_DWARF|3" "RACE_NIGHTELF|4" "RACE_UNDEAD|5" "RACE_TROLL|8")
  string(REPLACE "|" ";" parts "${race}")
  list(GET parts 0 race_name)
  list(GET parts 1 race_id)
  string(REGEX MATCH "${race_name} +=  *${race_id}," race_match "${shared_defines}")
  if(NOT race_match)
    message(FATAL_ERROR "Race id changed: ${race_name} is not ${race_id} (D20 masks)")
  endif()
endforeach()
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskUndead = 16;" "D20 Undead mask")
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskTroll = 128;" "D20 Troll mask")
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskNightElf = 8;" "D20 Night Elf mask")
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskHumanDwarf = 5;" "D20 Human+Dwarf mask")

region("${priest_strategy}" "bool ai::dietpriest::RaceMayLearn(PlayerbotAI* ai, uint32 raceMask)" "\n}" race_gate)
require_order("${race_gate}" "if (!sPlayerbotAIConfig.dietPriestRacials)\n        return true;" "bot->getRaceMask()" "D20 switch off registers every node before any race read")
require_text("${race_gate}" "return (bot->getRaceMask() & raceMask) != 0;" "D20 race mask test")

region("${priest_strategy}" "#ifdef MANGOSBOT_ZERO" "#ifdef MANGOSBOT_ONE" priest_zero)
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskHumanDwarf)" "critical health" "desperate prayer" "ACTION_EMERGENCY" "D20 desperate prayer")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskNightElf)" "target of attacker" "elune's grace" "ACTION_HIGH + 3" "D20 elune's grace")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskUndead)" "touch of weakness" "touch of weakness" "ACTION_NORMAL + 1" "D20 touch of weakness")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskTroll)" "shadowguard" "shadowguard" "ACTION_NORMAL + 1" "D20 shadowguard")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskNightElf)" "very often" "starshards" "ACTION_NORMAL + 1" "D20 starshards")
# The other nodes on the shared triggers stay ungated.
require_text("${priest_zero}" "\n    triggers.push_back(new TriggerNode(\n        \"critical health\",\n        NextAction::array(0, new NextAction(\"flash heal\", ACTION_CRITICAL_HEAL), NULL)));" "D20 flash heal stays")
require_text("${priest_zero}" "\n    triggers.push_back(new TriggerNode(\n        \"target of attacker\",\n        NextAction::array(0, new NextAction(\"power word: shield\", ACTION_HIGH), NULL)));" "D20 power word: shield stays")

region("${shadow_strategy}" "#ifdef MANGOSBOT_ZERO" "#ifdef MANGOSBOT_ONE" shadow_zero)
require_gated_node("${shadow_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskUndead)" "devouring plague" "devouring plague" "ACTION_NORMAL + 2" "D20 devouring plague")
require_gated_node("${shadow_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskTroll)" "hex of weakness" "hex of weakness" "ACTION_NORMAL + 2" "D20 hex of weakness")

require_switch_first_trigger("${priest_triggers_h}" "DevouringPlagueTrigger" "DebuffTrigger" "devouring plague" "dietPriestRacials" "D20 devouring plague trigger")
require_switch_first_trigger("${priest_triggers_h}" "TouchOfWeaknessTrigger" "BuffTrigger" "touch of weakness" "dietPriestRacials" "D20 touch of weakness trigger")
require_switch_first_trigger("${priest_triggers_h}" "HexOfWeaknessTrigger" "DebuffTrigger" "hex of weakness" "dietPriestRacials" "D20 hex of weakness trigger")
require_switch_first_trigger("${priest_triggers_h}" "ShadowguardTrigger" "BuffTrigger" "shadowguard" "dietPriestRacials" "D20 shadowguard trigger")
forbid_text("${priest_triggers_h}" "BUFF_TRIGGER(TouchOfWeaknessTrigger" "D20 old macro trigger")
forbid_text("${priest_triggers_h}" "DEBUFF_TRIGGER(DevouringPlagueTrigger" "D20 old macro trigger")

message(STATUS "DIET_G2_SOURCE_CONTRACT=PASS")
