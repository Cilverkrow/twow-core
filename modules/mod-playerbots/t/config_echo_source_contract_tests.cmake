cmake_policy(VERSION 3.16)
# twow-repo#601 R01-A/B (owner rule 18.10.2026, twow-repo#615): one "[ConfigEcho] <full key>=<parsed value> ..." line at the
# end of PlayerbotAIConfig::Initialize, so every test arm proves its A/B switches in the running process. Values come from
# the parsed members (after clamps / defaults), never from a second config read; one line per start, no key, no state.
# Reads named files only.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 ConfigEcho: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 ConfigEcho: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 ConfigEcho: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nlen)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR from "${at} + ${nlen}")
    string(SUBSTRING "${rest}" ${from} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "#601 ConfigEcho: expected ${expected}x ${description}, found ${n}")
  endif()
endfunction()

read_source("ConfigEchoPolicy.h" policy)
read_source("PlayerbotAIConfig.cpp" config_cpp)

# 1. Pure formatter: std only, no logging, the tag and the key=value shape.
forbid_text("${policy}" "#include \"" "game include in the policy")
forbid_text("${policy}" "sLog" "logging in the policy")
require_text("${policy}" "namespace ai::config_echo" "policy namespace")
require_text("${policy}" "std::string line = \"[ConfigEcho]\";" "line tag")
require_text("${policy}" "line += \" AiPlayerbot.\";\n        line += e.first;\n        line += '=';\n        line += e.second;" "one ' AiPlayerbot.<key>=value' token per entry")

# 2. One echo, at the end of Initialize: after the last loader, before the banner; nothing else prints it.
require_text("${config_cpp}" "#include \"playerbot/ConfigEchoPolicy.h\"" "policy include")
count_text("${config_cpp}" "ai::config_echo::FormatLine(" 1 "echo call")
require_order("${config_cpp}" "sRandomPlayerbotMgr.LoadBattleMastersCache();" "ai::config_echo::FormatLine(" "echo after the last loader")
require_order("${config_cpp}" "ai::config_echo::FormatLine(" "        AI Playerbot initialized       " "echo before the banner")
string(FIND "${config_cpp}" "ai::config_echo::FormatLine(" echo_at)
string(SUBSTRING "${config_cpp}" ${echo_at} -1 echo_rest)
string(FIND "${echo_rest}" "}).c_str());" echo_end)
if(echo_end EQUAL -1)
  message(FATAL_ERROR "#601 ConfigEcho: echo block end not found")
endif()
string(SUBSTRING "${echo_rest}" 0 ${echo_end} echo)
forbid_text("${echo}" "config.Get" "a second config read in the echo (values must be the parsed members)")
forbid_text("${echo}" "sConfig" "a raw config read in the echo")
# entries carry the key without the prefix, so every other contract that counts a quoted full key in this file stays at 1
forbid_text("${echo}" "\"AiPlayerbot." "a quoted full key in the echo (FormatLine adds the prefix)")
require_text("${config_cpp}" "sLog.outString(\"%s\", ai::config_echo::FormatLine({" "echo printed with outString")

# 3. Every echoed key: the entry names the key (without "AiPlayerbot.") and its parsed member, and that member is parsed from that key.
#    Keys the R01-A/B arms prove (R01 steps + pins), P3-a long (CLI-PATH) and #342 (S3b / flights).
set(entries
  "QuestFirstProgression.Enabled|questFirstProgressionEnabled"
  "QuestFirstProgression.FailureClasses|questFirstProgressionFailureClasses"
  "QuestFirstProgression.ResetOnProgressEvents|questFirstProgressionResetOnProgressEvents"
  "QuestFirstProgression.AbandonStructuralParked|questFirstProgressionAbandonStructuralParked"
  "QuestFirstProgression.TransientCountsAfter|questFirstProgressionTransientCountsAfter"
  "QuestFirstProgression.HubPreloopSameFilters|questFirstProgressionHubPreloopSameFilters"
  "QuestFirstProgression.GrindMode|questFirstProgressionGrindMode"
  "QuestFirstProgression.GrindFallbackMinutes|questFirstProgressionGrindFallbackMinutes"
  "QuestFirstProgression.GrindFallbackFailures|questFirstProgressionGrindFallbackFailures"
  "QuestFirstProgression.BandLadder|questFirstProgressionBandLadder"
  "QuestFirstProgression.TurnInParkFailures|questFirstProgressionTurnInParkFailures"
  "QuestFirstProgression.MinLevelForCrossMapQuestRoute|questFirstProgressionMinLevelForCrossMapQuestRoute"
  "QuestRescue.KeepRequestWhenFull|questRescueKeepRequestWhenFull"
  "QuestFeasibility.Enable|questFeasibilityEnable"
  "QuestFeasibility.SoloSkipGroupQuests|questFeasibilitySoloSkipGroupQuests"
  "ZoneExit.TurnInStallCounts|zoneExitTurnInStallCounts"
  "ZoneExit.TurnInTravelStall|zoneExitTurnInTravelStall"
  "ZoneExit.TurnInNearRadius|zoneExitTurnInNearRadius"
  "QuestFirstProgression.S1Cohort|questFirstProgressionS1Cohort"
  "QuestFirstProgression.S3Cohort|questFirstProgressionS3Cohort"
  "DirectTargets.Enable|directTargetsEnabled"
  "DirectTargets.FreshBackoffSeconds|directTargetsFreshBackoffSeconds"
  "DirectTargets.FreshGrindFailures|directTargetsFreshGrindFailures"
  "Path.TaxiCancelForm|pathTaxiCancelForm"
  "Path.TransportEdgeTrace|pathTransportEdgeTrace"
  "Path.TransportDeckProbe|pathTransportDeckProbe"
  "Path.TransportWaitTimeoutMin|pathTransportWaitTimeoutMin"
  "Rotation.EmergencyLadder|rotationEmergencyLadder"
  "Rotation.SpecFromPath|rotationSpecFromPath"
  "Rotation.RestGate|rotationRestGate"
  "Gather.FishingSkillCheck|gatherFishingSkillCheck"
  "Perf.BindAreaFromHomebind|perfBindAreaFromHomebind"
  "Loot.RoleWeights|lootRoleWeights"
  "BotUpdateTrace|botUpdateTrace"
  "ActivityTrace|activityTrace"
  "EnableActionLog|enableActionLog"
  "LogInGroupOnly|logInGroupOnly"
  "Grind.GreyByLevel|grindGreyByLevel")
foreach(entry IN LISTS entries)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 key)
  list(GET parts 1 member)
  require_text("${echo}" "{\"${key}\", Value(${member})}," "echo entry for ${key}")
  count_text("${echo}" "\"${key}\"" 1 "echo entry ${key}")
  string(REPLACE "." "\\." key_re "${key}")
  string(REGEX MATCH "\n    ${member} = [^\n]*\"AiPlayerbot\\.${key_re}\"" parsed "${config_cpp}")
  if(parsed STREQUAL "")
    message(FATAL_ERROR "#601 ConfigEcho: ${member} is not parsed from AiPlayerbot.${key}")
  endif()
endforeach()
# list keys echo their parsed size
require_text("${echo}" "{\"QuestFeasibility.Blocklist.count\", Value(uint32(questFeasibilityBlocklist.size()))}," "Blocklist size")
require_text("${echo}" "{\"QuestFeasibility.BlocklistWhileNoCustomStarts.count\", Value(uint32(questFeasibilityBlocklistWhileNoCustomStarts.size()))}," "BlocklistWhileNoCustomStarts size")
require_text("${echo}" "{\"ActionLog.Guids.count\", Value(uint32(actionLogGuids.size()))}," "ActionLog.Guids size")
string(REGEX MATCH "
 *actionLogGuids = [^
]*\"AiPlayerbot\.ActionLog\.Guids\"" parsed_guids "${config_cpp}")
if(parsed_guids STREQUAL "")
  message(FATAL_ERROR "#601 ConfigEcho: actionLogGuids is not parsed from AiPlayerbot.ActionLog.Guids")
endif()
# every entry in the echo is listed above (no unpinned entry slips in)
string(REGEX MATCHALL "\\{\"[^\"]+\", " all_entries "${echo}")
list(LENGTH all_entries n_entries)
list(LENGTH entries n_listed)
math(EXPR n_expected "${n_listed} + 3")
if(NOT n_entries EQUAL n_expected)
  message(FATAL_ERROR "#601 ConfigEcho: ${n_entries} echo entries, ${n_expected} pinned here - add the new entry to this contract")
endif()
