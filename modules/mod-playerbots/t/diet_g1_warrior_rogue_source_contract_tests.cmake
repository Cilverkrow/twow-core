cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g1 (warrior + rogue), every fix behind its own AiPlayerbot.Diet.* switch,
# default 0 = today's trigger lists. Strategies build their lists at engine init, so a switch takes effect at the
# next engine init. Each block checks: default off (header, reader, dist), the gate, and the kept off path.
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

# Switch: member defaults to false, reader defaults to false, dist documents 0.
function(require_switch member key)
  require_text("${config_h}" "    bool ${member} = false;  // twow-repo#568 (diet " "${member} member default false")
  require_text("${config_cpp}" "    ${member} = config.GetBoolDefault(\"AiPlayerbot.${key}\", false);" "${key} read with default off")
  require_text("${config_dist}" "\nAiPlayerbot.${key} = 0\n" "${key} = 0 in the dist config")
endfunction()

# Only the vanilla (MANGOSBOT_ZERO) section of a strategy file is built here.
function(read_vanilla path out)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  region("${text}" "#ifdef MANGOSBOT_ZERO" "#endif" body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# D03: combat rogue boost cooldowns registered in the combat engine (on) or, as today, the non-combat engine (off).
require_switch(dietRogueCombatBoostInCombat Diet.RogueCombatBoostInCombat)
read_vanilla(strategy/rogue/CombatRogueStrategy.cpp combat_rogue)
region("${combat_rogue}" "void CombatRogueBoostStrategy::InitCombatTriggers(" "void CombatRogueBoostStrategy::InitNonCombatTriggers(" d03_combat)
require_order("${d03_combat}" "if (sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"adrenaline rush\", ACTION_HIGH + 5)" "D03 adrenaline rush in combat only behind the switch")
require_order("${d03_combat}" "if (sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"blade flurry\", ACTION_HIGH + 4)" "D03 blade flurry in combat only behind the switch")
forbid_text("${d03_combat}" "!sPlayerbotAIConfig.dietRogueCombatBoostInCombat" "D03 inverted gate in the combat list")
region("${combat_rogue}" "void CombatRogueBoostStrategy::InitNonCombatTriggers(" "void CombatRogueBoostPveStrategy::InitCombatTriggers(" d03_nc)
require_order("${d03_nc}" "if (!sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"adrenaline rush\", ACTION_HIGH + 5)" "D03 off path keeps adrenaline rush in the non-combat list")
require_order("${d03_nc}" "if (!sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"blade flurry\", ACTION_HIGH + 4)" "D03 off path keeps blade flurry in the non-combat list")

message(STATUS "DIET_G1_WARRIOR_ROGUE_SOURCE_CONTRACT=PASS")
