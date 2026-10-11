cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g4 (hunter + generic). Every fix sits behind its own switch
# AiPlayerbot.Diet.<Key>, default 0; with 0 the old trigger/strategy/action path runs unchanged.
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
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterStrategy.cpp" hunter_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterTriggers.cpp" hunter_triggers_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterActions.cpp" hunter_actions_cpp)

# Only the vanilla block of HunterStrategy.cpp is compiled in this build.
region("${hunter_strategy}" "#ifdef MANGOSBOT_ZERO" "#endif" hunter_zero)
require_text("${hunter_strategy}" "#include \"playerbot/PlayerbotAIConfig.h\"" "HunterStrategy.cpp includes the config")

# Every switch defaults to 0.
foreach(pair
    "dietHunterBestSting|HunterBestSting")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 member)
  list(GET parts 1 key)
  require_text("${config_h}" "bool ${member} = false;" "${member} member default false")
  require_text("${config_cpp}" "${member} = config.GetBoolDefault(\"AiPlayerbot.Diet.${key}\", false);" "${key} read with default 0")
  require_text("${config_dist}" "\nAiPlayerbot.Diet.${key} = 0\n" "${key} = 0 in the dist config")
endforeach()

# ---------------------------------------------------------------------------------------------------------
# D10 (AiPlayerbot.Diet.HunterBestSting): one 'no stings' node, trigger only when a sting can be used,
# serpent sting on mana targets while viper sting is unknown.
region("${hunter_zero}" "void HunterStingStrategy::InitCombatTriggers" "\n}\n" sting_base)
require_text("${sting_base}" "    if (!sPlayerbotAIConfig.dietHunterBestSting)\n    {\n        triggers.push_back(new TriggerNode(\n            \"no stings\",\n            NextAction::array(0, new NextAction(\"viper sting\", ACTION_NORMAL + 5), NULL)));\n    }" "D10 base viper node only with the switch off")
forbid_text("${sting_base}" "\n    triggers.push_back(new TriggerNode(\n        \"no stings\"" "D10 ungated base 'no stings' node")
require_text("${sting_base}" "\"serpent sting on attacker\", ACTION_HIGH + 2" "D10 on-attacker serpent node kept")
require_text("${sting_base}" "\"viper sting on attacker\", ACTION_HIGH + 1" "D10 on-attacker viper node kept")

set(both_stings "    if (sPlayerbotAIConfig.dietHunterBestSting)\n    {\n        triggers.push_back(new TriggerNode(\n            \"no stings\",\n            NextAction::array(0, new NextAction(\"viper sting\", ACTION_NORMAL + 5), new NextAction(\"serpent sting\", ACTION_NORMAL + 4), NULL)));\n        return;\n    }\n\n    triggers.push_back(new TriggerNode(\n        \"no stings\",\n        NextAction::array(0, new NextAction(\"serpent sting\", ACTION_NORMAL + 4), NULL)));")
foreach(part "Pvp" "Pve")
  region("${hunter_zero}" "void HunterSting${part}Strategy::InitCombatTriggers" "\n}\n" sting_part)
  require_text("${sting_part}" "${both_stings}" "D10 ${part}: one node with both stings (switch on), the old serpent node (switch off)")
endforeach()
region("${hunter_zero}" "void HunterStingRaidStrategy::InitCombatTriggers" "\n}\n" sting_raid)
require_text("${sting_raid}" "    if (sPlayerbotAIConfig.dietHunterBestSting)\n    {\n        triggers.push_back(new TriggerNode(\n            \"no stings\",\n            NextAction::array(0, new NextAction(\"viper sting\", ACTION_NORMAL + 5), NULL)));\n    }" "D10 raid: viper node only with the switch on")
forbid_text("${sting_raid}" "serpent sting" "D10 raid gets no serpent sting")

region("${hunter_triggers_cpp}" "bool HunterNoStingsActiveTrigger::IsActive()" "\n}\n" no_stings)
require_order("${no_stings}" "!ai->HasAura(\"viper sting\", target);" "if (!noStings || !sPlayerbotAIConfig.dietHunterBestSting)\n        return noStings;" "D10 old check first, then the switch")
require_order("${no_stings}" "if (!noStings || !sPlayerbotAIConfig.dietHunterBestSting)\n        return noStings;" "AI_VALUE2(uint32, \"spell id\", \"viper sting\")" "D10 spell lookups only with the switch on")
require_text("${no_stings}" "AI_VALUE2(uint8, \"health\", \"current target\") > 40" "D10 old 40 % health check kept")
require_text("${no_stings}" "if (viperKnown && targetHasMana)\n        return true;" "D10 viper case")
require_text("${no_stings}" "return AI_VALUE2(uint8, \"health\", \"current target\") > 50 && (!targetHasMana || !viperKnown) &&\n        AI_VALUE2(uint32, \"spell id\", \"serpent sting\") != 0;" "D10 serpent case mirrors serpent isUseful")

region("${hunter_actions_cpp}" "bool CastSerpentStingAction::isUseful()" "\n}\n" serpent_useful)
require_order("${serpent_useful}" "(!(AI_VALUE2(uint8, \"mana\", GetTargetName()) >= 10) || !AI_VALUE2(uint32, \"spell id\", \"viper sting\"))" "return CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, \"health\", GetTargetName()) > 50 && !(AI_VALUE2(uint8, \"mana\", GetTargetName()) >= 10);" "D10 serpent: switch branch, then the old line")
require_text("${serpent_useful}" "if (sPlayerbotAIConfig.dietHunterBestSting)" "D10 serpent switch")
region("${hunter_actions_cpp}" "bool CastViperStingAction::isUseful()" "\n}\n" viper_useful)
require_order("${viper_useful}" "if (sPlayerbotAIConfig.dietHunterBestSting && !AI_VALUE2(uint32, \"spell id\", \"viper sting\"))\n        return false;" "return CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, \"mana\", GetTargetName()) >= 10;" "D10 viper: known gate, then the old line")

message(STATUS "DIET_G4_HUNTER_GENERIC_SOURCE_CONTRACT=PASS")
