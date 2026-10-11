cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g3 (shaman, paladin, druid). Every fix sits behind its own switch
# AiPlayerbot.Diet.<Key>, default 0 = exactly the old code path. This contract checks per fix: the switch is
# read with default false and documented as 0, the gate is in place, and the old (OFF) path is still there.
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

function(read_source relative out)
  file(READ "${PB_SOURCE_DIR}/${relative}" content)
  set(${out} "${content}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" config_dist)

# One switch: member defaults to false, read with default false, documented as 0 in the dist config.
function(require_diet_switch member key)
  require_text("${config_h}" "    bool ${member} = false;" "${key} member default off")
  require_text("${config_cpp}" "    ${member} = config.GetBoolDefault(\"AiPlayerbot.Diet.${key}\", false);" "${key} read with default off")
  require_text("${config_dist}" "\nAiPlayerbot.Diet.${key} = 0\n" "${key} = 0 in the dist config")
  forbid_text("${config_dist}" "\nAiPlayerbot.Diet.${key} = 1" "${key} enabled in the dist config")
endfunction()

# ---------------------------------------------------------------------------------------------------------
# D04 (AiPlayerbot.Diet.LevelingDruidWrath): leveling druid Wrath node on the registered trigger name.
require_diet_switch(dietLevelingDruidWrath LevelingDruidWrath)
read_source("strategy/druid/LevelingDruidStrategy.cpp" leveling_druid)
read_source("strategy/triggers/TriggerContext.h" trigger_ctx)
require_text("${trigger_ctx}" "creators[\"enemy out of melee\"] =" "registered trigger name used by the ON path")
forbid_text("${trigger_ctx}" "creators[\"enemy out of melee range\"]" "creator for the old name (the OFF path would no longer be dead)")
region("${leveling_druid}" "void LevelingDruidStrategy::InitCombatTriggers(" "\n}" leveling_combat)
require_text("${leveling_combat}" "sPlayerbotAIConfig.dietLevelingDruidWrath ? \"enemy out of melee\" : \"enemy out of melee range\"," "D04 gate with the old name as OFF path")
require_order("${leveling_combat}" "sPlayerbotAIConfig.dietLevelingDruidWrath" "new NextAction(\"wrath\", ACTION_NORMAL)" "D04 gate on the Wrath node")

message(STATUS "DIET_G3_SOURCE_CONTRACT=PASS")
