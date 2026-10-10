# twow-repo#541 (audit A23, audit cards 5 + 46): behind AiPlayerbot.Perf.AttackersSingleList (default 0)
# "has attackers" and the one-target variant of "possible attack targets" read the full "attackers"
# list instead of a second, separate "attackers::1" AttackersValue pass. Only emptiness of the getOne
# lists is consumed (has attackers, has possible attack targets, has enemy player targets). Combat start
# by the bot itself (PlayerbotAI::OnCombatStarted) resets the full list instead of attackers::1.
# Switch 0 keeps every old line verbatim. Behaviour-changing within the 1 s value cache granularity.
# Every check returns a gap text; the real sources must have none, the negative probes must have one.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Body between start_marker and end_marker; empty when a marker is missing (the checks then report it).
function(region text start_marker end_marker out)
  set(${out} "" PARENT_SCOPE)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(gap_unless_has text needle description)
  string(FIND "${text}" "${needle}" _off)
  if(_off EQUAL -1)
    set(gaps "${gaps}missing ${description}; " PARENT_SCOPE)
  endif()
endfunction()

function(gap_if_has text needle description)
  string(FIND "${text}" "${needle}" _off)
  if(NOT _off EQUAL -1)
    set(gaps "${gaps}forbidden ${description}; " PARENT_SCOPE)
  endif()
endfunction()

function(gap_unless_order text first second description)
  string(FIND "${text}" "${first}" _a)
  string(FIND "${text}" "${second}" _b)
  if(_a EQUAL -1 OR _b EQUAL -1 OR NOT _a LESS _b)
    set(gaps "${gaps}order ${description}; " PARENT_SCOPE)
  endif()
endfunction()

set(gate "sPlayerbotAIConfig.perfAttackersSingleList")
set(full_get "context->GetValue<std::list<ObjectGuid>>(\"attackers\")->Get().empty()")
set(one_get "context->GetValue<std::list<ObjectGuid>>(\"attackers\", 1)->Get().empty()")

# --- 1. Switch: member default off, read with default false, documented as 0 ---
function(check_switch config_h config_cpp conf_dist out)
  set(gaps "")
  gap_unless_has("${config_h}" "bool perfAttackersSingleList = false;" "member default off")
  gap_unless_order("${config_h}" "uint32 aiDelayJitterPct = 0;" "bool perfAttackersSingleList = false;" "member after the #541 switches")
  gap_unless_has("${config_cpp}" "perfAttackersSingleList = config.GetBoolDefault(\"AiPlayerbot.Perf.AttackersSingleList\", false);" "config read with default false")
  gap_unless_has("${conf_dist}" "\nAiPlayerbot.Perf.AttackersSingleList = 0\n" "documented key with value 0")
  gap_if_has("${conf_dist}" "AiPlayerbot.Perf.AttackersSingleList = 1" "key shipped as on")
  set(${out} "${gaps}" PARENT_SCOPE)
endfunction()

# --- 2. HasAttackersValue: gated full-list read first, old attackers::1 read kept as the off path ---
function(check_has_attackers counts_cpp out)
  set(gaps "")
  region("${counts_cpp}" "bool HasAttackersValue::Calculate()" "bool HasPossibleAttackTargetsValue::Calculate()" body)
  gap_unless_has("${body}" "if (${gate})\n        return !${full_get};" "gated full-list read")
  gap_unless_has("${body}" "return !${one_get};" "old attackers::1 read on the off path")
  gap_unless_order("${body}" "if (${gate})" "return !${one_get};" "switch before the old read")
  region("${body}" "if (${gate})" "return !${one_get};" on_path)
  gap_if_has("${on_path}" "\"attackers\", 1" "attackers::1 on the on path")
  gap_if_has("${body}" "static" "static state")
  gap_if_has("${body}" "mutex" "locks")
  set(${out} "${gaps}" PARENT_SCOPE)
endfunction()

# --- 3. PossibleAttackTargetsValue: getOne attempt gated, full fallback keeps the getOne flag ---
function(check_possible_attack_targets pat_cpp out)
  set(gaps "")
  region("${pat_cpp}" "std::list<ObjectGuid> PossibleAttackTargetsValue::Calculate()" "void PossibleAttackTargetsValue::RemoveNonThreating(" body)
  gap_unless_has("${body}" "if (getOne && !${gate})" "gated getOne attempt")
  gap_if_has("${body}" "if (getOne)" "ungated getOne attempt")
  gap_unless_has("${body}" "result = AI_VALUE2(std::list<ObjectGuid>, \"attackers\", 1);\n                RemoveNonThreating(result, getOne);" "old attackers::1 attempt on the off path")
  gap_unless_has("${body}" "if (result.empty())\n            {\n                result = AI_VALUE(std::list<ObjectGuid>, \"attackers\");\n                RemoveNonThreating(result, getOne);" "full fallback with the getOne flag")
  gap_unless_order("${body}" "if (getOne && !${gate})" "if (result.empty())" "attempt before the fallback")
  gap_if_has("${body}" "static" "static state")
  gap_if_has("${body}" "mutex" "locks")
  set(${out} "${gaps}" PARENT_SCOPE)
endfunction()

# --- 4. OnCombatStarted: full list reset on the on path, attackers::1 on the off path, has attackers always ---
function(check_combat_start_reset ai_cpp out)
  set(gaps "")
  region("${ai_cpp}" "void PlayerbotAI::OnCombatStarted()" "void PlayerbotAI::OnCombatEnded()" body)
  gap_unless_has("${body}" "        if (${gate})\n            aiObjectContext->GetValue<std::list<ObjectGuid>>(\"attackers\")->Reset();\n        else\n            aiObjectContext->GetValue<std::list<ObjectGuid>>(\"attackers\", 1)->Reset();\n        aiObjectContext->GetValue<bool>(\"has attackers\")->Reset();" "gated reset block")
  gap_unless_order("${body}" "aiObjectContext->GetValue<bool>(\"has attackers\")->Reset();" "ChangeEngine(BotState::BOT_STATE_COMBAT);" "resets before the engine change")
  set(${out} "${gaps}" PARENT_SCOPE)
endfunction()

# --- 5. EnemyPlayersValue stays untouched (its getOne chain now ends in the full list by itself) ---
function(check_enemy_players_untouched ep_cpp out)
  set(gaps "")
  gap_unless_has("${ep_cpp}" "            if (getOne)\n            {\n                // Try to get one enemy target\n                result = AI_VALUE2(std::list<ObjectGuid>, \"possible attack targets\", 1);\n                ApplyFilter(result, getOne);" "unchanged enemy-players getOne attempt")
  gap_unless_has("${ep_cpp}" "return !context->GetValue<std::list<ObjectGuid>>(\"enemy player targets\", 1)->Get().empty();" "unchanged has enemy player targets")
  gap_if_has("${ep_cpp}" "perfAttackersSingleList" "switch in EnemyPlayerValue.cpp")
  set(${out} "${gaps}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/values/AttackerCountValues.cpp" counts_cpp)
read_source("strategy/values/PossibleAttackTargetsValue.cpp" pat_cpp)
read_source("strategy/values/EnemyPlayerValue.cpp" ep_cpp)

# Real sources: no gaps.
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" g1)
check_has_attackers("${counts_cpp}" g2)
check_possible_attack_targets("${pat_cpp}" g3)
check_combat_start_reset("${ai_cpp}" g4)
check_enemy_players_untouched("${ep_cpp}" g5)
set(all_gaps "${g1}${g2}${g3}${g4}${g5}")
if(NOT all_gaps STREQUAL "")
  message(FATAL_ERROR "#541 A23 attackers single list: ${all_gaps}")
endif()

# Negative probe 1: the getOne attempt without the gate (attackers::1 pass still runs) must be caught.
string(REPLACE "if (getOne && !${gate})" "if (getOne)" probe "${pat_cpp}")
check_possible_attack_targets("${probe}" probe_gap)
if(probe_gap STREQUAL "")
  message(FATAL_ERROR "#541 A23: negative probe not caught: ungated attackers::1 attempt - the scan is broken")
endif()

# Negative probe 2: the fallback losing the getOne flag (full RemoveNonThreating pass) must be caught.
string(REPLACE "result = AI_VALUE(std::list<ObjectGuid>, \"attackers\");\n                RemoveNonThreating(result, getOne);" "result = AI_VALUE(std::list<ObjectGuid>, \"attackers\");\n                RemoveNonThreating(result, false);" probe "${pat_cpp}")
check_possible_attack_targets("${probe}" probe_gap)
if(probe_gap STREQUAL "")
  message(FATAL_ERROR "#541 A23: negative probe not caught: fallback without getOne - the scan is broken")
endif()

# Negative probe 3: the on path still reading attackers::1 must be caught.
string(REPLACE "if (${gate})\n        return !${full_get};" "if (${gate})\n        return !${one_get};" probe "${counts_cpp}")
check_has_attackers("${probe}" probe_gap)
if(probe_gap STREQUAL "")
  message(FATAL_ERROR "#541 A23: negative probe not caught: on path reads attackers::1 - the scan is broken")
endif()

# Negative probe 4: combat start resetting only attackers::1 (stale full list -> instant 'combat end') must be caught.
string(REPLACE "        if (${gate})\n            aiObjectContext->GetValue<std::list<ObjectGuid>>(\"attackers\")->Reset();\n        else\n" "" probe "${ai_cpp}")
check_combat_start_reset("${probe}" probe_gap)
if(probe_gap STREQUAL "")
  message(FATAL_ERROR "#541 A23: negative probe not caught: no full-list reset at combat start - the scan is broken")
endif()

# Negative probe 5: the key shipped as on must be caught.
string(REPLACE "\nAiPlayerbot.Perf.AttackersSingleList = 0\n" "\nAiPlayerbot.Perf.AttackersSingleList = 1\n" probe "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${probe}" probe_gap)
if(probe_gap STREQUAL "")
  message(FATAL_ERROR "#541 A23: negative probe not caught: key shipped as on - the scan is broken")
endif()

message(STATUS "attackers_single_list source contract passed")
