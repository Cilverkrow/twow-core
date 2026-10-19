# twow-repo#601 / #614 (L1 observation 19.10.2026): grey is judged by the mob's level (QuestSearchPolicy.h GreyByLevel), never by
# MaNGOS::XP::Gain - that one is 0 for every mob nobody has damaged yet (damage-origin share outside dungeons). Diagnostics
# ([GreyTargets], [GrindPick], [GrindWindow]) always by level; the #421 grey skip by level with AiPlayerbot.Grind.GreyByLevel.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#614 grey by level: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#614 grey by level: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" len)
  set(n 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "#614 grey by level: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("QuestSearchPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/GrindTargetValue.cpp" grind_cpp)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_cpp)

require_text("${policy}" "inline bool GreyByLevel(uint32_t mobLevel, uint32_t grayLevel)\n{\n    return mobLevel <= grayLevel;\n}" "grey = mob level <= grey level")
require_text("${config_h}" "bool grindGreyByLevel = false;" "default off")
require_text("${config_cpp}" "grindGreyByLevel = config.GetBoolDefault(\"AiPlayerbot.Grind.GreyByLevel\", false);" "config read")
require_text("${conf_dist}" "AiPlayerbot.Grind.GreyByLevel = 0" "documented, 0 by default")

set(by_level "ai::quest_search::GreyByLevel(creature->GetLevel(), MaNGOS::XP::GetGrayLevel(bot->GetLevel()))")
# Diagnostics: [GreyTargets] and the #421 skip in GrindTargetValue, [GrindPick] and [GrindWindow] in ChooseTargetActions - by level.
count_text("${grind_cpp}" "${by_level}" 2 "grey by level in GrindTargetValue ([GreyTargets] + #421 skip)")
count_text("${choose_cpp}" "${by_level}" 2 "grey by level in ChooseTargetActions ([GrindPick] + [GrindWindow])")
require_text("${grind_cpp}" "if (creature && creature->GetObjectGuid() != lastGreyTarget &&\n        ${by_level})" "[GreyTargets] by level")
require_text("${choose_cpp}" "pickGrey = creature && ${by_level};" "[GrindPick] by level")
require_text("${choose_cpp}" "gapGrey = creature && ${by_level};" "[GrindWindow] by level")
# XP::Gain only in the switch-off branch of the #421 skip, nowhere else.
count_text("${grind_cpp}" "MaNGOS::XP::Gain(" 1 "XP::Gain only in the switch-off branch")
count_text("${choose_cpp}" "MaNGOS::XP::Gain(" 0 "no XP::Gain grey test in ChooseTargetActions")
require_text("${grind_cpp}" "else if (creature && (sPlayerbotAIConfig.grindGreyByLevel ?\n                    ${by_level} :\n                    !MaNGOS::XP::Gain(bot, creature)) &&" "the #421 skip: by level with the switch, XP::Gain as before without")
require_order("${grind_cpp}" "sPlayerbotAIConfig.grindGreyByLevel ?" "!MaNGOS::XP::Gain(bot, creature)" "switch before the old test")

message(STATUS "grey_by_level source contract passed")
