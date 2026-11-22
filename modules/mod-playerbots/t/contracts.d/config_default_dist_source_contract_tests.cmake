cmake_policy(VERSION 3.16)
# twow-core#1156 (CFG-DEFAULT-01): the code default of these playerbot keys equals the value the shipped aiplayerbot.conf.dist.in
# carries, so a config without the key behaves like the rendered config and the two cannot drift apart again. Pinned here:
#   (a) for each key below, "AiPlayerbot.<Key> = <n>" appears exactly once in the dist and the loader reads
#       GetBoolDefault("AiPlayerbot.<Key>", <true|false>) with true = (n != 0);
#   (b) the six keys are 1 in the dist and true in the code (the applied value in REF v3 and the live render of 18.10.);
#   (c) negative probes: a flipped code default and a flipped dist value are each caught by the same check.
# Not in this list on purpose: AiPlayerbot.Enabled (code false is the fail-safe: a missing key must not start the bots) and
# AiPlayerbot.RandomBotSayWithoutMaster (dist 1, code and profile 0, OB-10 decides). The float keys 0.0f vs 0 are the same value.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "config default dist: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# check_key(<code text> <dist text> <key> <result var>): result "" when the code default equals the dist value, else the reason.
function(check_key code dist key out_var)
  set(reason "")
  string(REGEX MATCHALL "\nAiPlayerbot\\.${key} = [0-9]+" dist_lines "${dist}")
  list(LENGTH dist_lines n_dist)
  if(NOT n_dist EQUAL 1)
    set(reason "${key}: expected exactly one dist line 'AiPlayerbot.${key} = <n>', found ${n_dist}")
  else()
    string(REGEX REPLACE ".*= ([0-9]+)$" "\\1" dist_value "${dist_lines}")
    string(REGEX MATCH "\"AiPlayerbot\\.${key}\", (true|false)\\)" code_match "${code}")
    if(NOT code_match)
      set(reason "${key}: no GetBoolDefault(\"AiPlayerbot.${key}\", true|false) in the loader")
    else()
      string(REGEX REPLACE ".*, (true|false)\\)$" "\\1" code_value "${code_match}")
      if(dist_value STREQUAL "0")
        set(want "false")
      else()
        set(want "true")
      endif()
      if(NOT code_value STREQUAL want)
        set(reason "${key}: dist value ${dist_value} but the code default is ${code_value} (want ${want})")
      endif()
    endif()
  endif()
  set(${out_var} "${reason}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.cpp" code)
read_source("aiplayerbot.conf.dist.in" dist)

set(keys ShowProgressBars AutoEquipUpgradeLoot DisableActivityPriorities EnableGreet RandomBotShowCloak RandomBotShowHelmet)
foreach(key IN LISTS keys)
  check_key("${code}" "${dist}" "${key}" reason)
  if(NOT reason STREQUAL "")
    message(FATAL_ERROR "config default dist: ${reason}")
  endif()
  # (b) the applied value is on
  if(NOT dist MATCHES "\nAiPlayerbot\\.${key} = 1\n")
    message(FATAL_ERROR "config default dist: ${key} is not 1 in the dist (the applied value, twow-core#1156)")
  endif()
  if(NOT code MATCHES "\"AiPlayerbot\\.${key}\", true\\)")
    message(FATAL_ERROR "config default dist: ${key} is not true in the loader (twow-core#1156)")
  endif()
endforeach()

# (c) negative probes: the check must fail on a mutated text
string(REPLACE "\"AiPlayerbot.EnableGreet\", true)" "\"AiPlayerbot.EnableGreet\", false)" code_flipped "${code}")
check_key("${code_flipped}" "${dist}" EnableGreet probe_code)
if(probe_code STREQUAL "")
  message(FATAL_ERROR "config default dist: probe 'code default flipped' was not caught")
endif()
string(REPLACE "\nAiPlayerbot.AutoEquipUpgradeLoot = 1\n" "\nAiPlayerbot.AutoEquipUpgradeLoot = 0\n" dist_flipped "${dist}")
check_key("${code}" "${dist_flipped}" AutoEquipUpgradeLoot probe_dist)
if(probe_dist STREQUAL "")
  message(FATAL_ERROR "config default dist: probe 'dist value flipped' was not caught")
endif()
string(REPLACE "\nAiPlayerbot.DisableActivityPriorities = 1\n" "\n" dist_missing "${dist}")
check_key("${code}" "${dist_missing}" DisableActivityPriorities probe_missing)
if(probe_missing STREQUAL "")
  message(FATAL_ERROR "config default dist: probe 'dist line missing' was not caught")
endif()

message(STATUS "CONFIG_DEFAULT_DIST_CONTRACT=PASS")
