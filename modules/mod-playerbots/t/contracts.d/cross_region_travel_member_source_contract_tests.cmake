cmake_policy(VERSION 3.16)
# twow-repo#710 (HARM-RACE, OB-00 order 11.11.2026; CLI project finding; same class as #707 / #697 / #563 part 4):
# Continents are split into regions; every region is its own Map object (same map id, own instance id) updated on its own pool thread.
# PlayerbotAI::IsSafe compares map id, instance id and the teleport flag only: a group member in the NEIGHBOURING region passes it. A bot
# must not read that member's quest map / group / AiObjectContext (a lazy GetValue is an insert = a write) from its own thread.
#
# Neighbour table (each named, changed or deliberately left):
#   CHANGED   TravelTarget::IsDestinationActive   (TravelMgr.cpp)  group copy: GetBotAI(member), PlayerTravelInfo(member) (quest map, group, AI)
#   CHANGED   TravelTarget::IsConditionsActive    (TravelMgr.cpp)  group copy: member's AiObjectContext, ClearValues, GetValue (insert)
#   CHANGED   TravelTarget::CheckStatus           (TravelMgr.cpp)  group copy: member->GetGroup()
#   CHANGED   ai::zone_keep follow-leave loop     (TravelMgr.cpp)  GetBotAI(member)->IsRealPlayer() for every group member; unknown = fail closed
#   LEFT      TravelMgr::Clear()                  manager / reload path: SetNullTravelTarget(plr) for all players; judged separately
#   LEFT      PlayerbotAI.cpp group copy debug text: GetGroupmember().GetPlayer()->GetName() (name scalar only)
#   LEFT      reads of the bot's OWN quest map / group (TravelMgr.cpp): own thread
# Negative probes: the same checkers must reject the old (IsSafe-only / GetBotAI(member)) texts and a reordered guard.

set(TAG "cross-region travel member (#710)")

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "${TAG}: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(region out text start_marker end_marker)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "${TAG}: cannot find ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "${TAG}: cannot find ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# `guard` must be present and come before every `reads` entry; each read must exist (the checker must not lose the site).
function(guard_before_reads errors_var text guard reads description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${guard}" guard_at)
  if(guard_at EQUAL -1)
    list(APPEND errors "${description}: missing guard '${guard}'")
  endif()
  foreach(read ${reads})
    string(FIND "${text}" "${read}" read_at)
    if(read_at EQUAL -1)
      list(APPEND errors "${description}: expected read '${read}' not found (the checker lost the site)")
    elseif(NOT guard_at EQUAL -1 AND NOT guard_at LESS read_at)
      list(APPEND errors "${description}: the guard '${guard}' must come before the read '${read}'")
    endif()
  endforeach()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

function(forbid errors_var text needle description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    list(APPEND errors "${description}: must not contain '${needle}'")
  endif()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

# ---- the checkers (each returns the list of violations, empty = ok) ----
function(group_copy_violations out fn_text what reads)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "!SameMapObject(bot, member)" "${reads}" "${what}")
  forbid(errors "${fn_text}" "if (!ai->IsSafe(member)) //unknown" "${what} (IsSafe alone is not the Map object)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(check_status_violations out fn_text)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "SameMapObject(bot, member)" "member->GetGroup()" "TravelTarget::CheckStatus")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(zone_keep_violations out fn_text)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "GetBotAIOnMapOf(bot, member)" "IsRealPlayer()" "zone_keep follow-leave member loop")
  forbid(errors "${fn_text}" "GetBotAI(member)" "zone_keep follow-leave member loop (no Map object test)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

# ---- the real source ----
read_source("TravelMgr.cpp" tm)
region(dest_fn "${tm}" "bool TravelTarget::IsDestinationActive()" "bool TravelTarget::IsConditionsActive(")
group_copy_violations(dest_errors "${dest_fn}" "TravelTarget::IsDestinationActive" "GetBotAI(player);PlayerTravelInfo(player")
region(cond_fn "${tm}" "bool TravelTarget::IsConditionsActive(" "void TravelTarget::OnNearTurnInStall(")
group_copy_violations(cond_errors "${cond_fn}" "TravelTarget::IsConditionsActive" "GetBotAI(player);GetAiObjectContext();ClearValues(;GetValue<bool>(")
region(status_fn "${tm}" "void TravelTarget::CheckStatus()" "if (!ai->HasStrategy(\"travel\", BotState::BOT_STATE_NON_COMBAT)")
check_status_violations(status_errors "${status_fn}")
region(zone_fn "${tm}" "bool realPlayerMember = false;" "if (!ai::zone_keep::LeaveAllowed(realPlayerMember))")
zone_keep_violations(zone_errors "${zone_fn}")

foreach(set_name dest cond status zone)
  if(${set_name}_errors)
    string(REPLACE ";" "\n  - " listed "${${set_name}_errors}")
    message(FATAL_ERROR "${TAG}: ${set_name}:\n  - ${listed}")
  endif()
endforeach()

# ---- negative probes ----
function(expect_rejected name errs)
  if(NOT errs)
    message(FATAL_ERROR "${TAG}: negative probe '${name}' was accepted - the checker no longer sees it")
  endif()
endfunction()

# 1. the old text: IsSafe only (a member in the neighbouring region passes it)
string(REPLACE "if (!ai->IsSafe(member) || !SameMapObject(bot, member)) //unknown" "if (!ai->IsSafe(member)) //unknown" probe_dest_old "${dest_fn}")
if("${probe_dest_old}" STREQUAL "${dest_fn}")
  message(FATAL_ERROR "${TAG}: probe 1 did not change the text (layout moved?)")
endif()
group_copy_violations(e1 "${probe_dest_old}" "TravelTarget::IsDestinationActive" "GetBotAI(player);PlayerTravelInfo(player")
expect_rejected("IsDestinationActive guarded by IsSafe only" "${e1}")

# 2. the same for IsConditionsActive (a context insert across threads)
string(REPLACE "if (!ai->IsSafe(member) || !SameMapObject(bot, member)) //unknown" "if (!ai->IsSafe(member)) //unknown" probe_cond_old "${cond_fn}")
if("${probe_cond_old}" STREQUAL "${cond_fn}")
  message(FATAL_ERROR "${TAG}: probe 2 did not change the text (layout moved?)")
endif()
group_copy_violations(e2 "${probe_cond_old}" "TravelTarget::IsConditionsActive" "GetBotAI(player);GetAiObjectContext();ClearValues(;GetValue<bool>(")
expect_rejected("IsConditionsActive guarded by IsSafe only" "${e2}")

# 3. the guard moved behind the first read (a guard that never gets a chance)
string(REPLACE "if (!ai->IsSafe(member) || !SameMapObject(bot, member)) //unknown" "if (!ai->IsSafe(member)) //unknown" probe_tmp3 "${dest_fn}")
string(REPLACE "    return tDestination->IsActive(player," "    if (!SameMapObject(bot, member)) return true;\n    return tDestination->IsActive(player," probe_dest_late "${probe_tmp3}")
group_copy_violations(e3 "${probe_dest_late}" "TravelTarget::IsDestinationActive" "GetBotAI(player);PlayerTravelInfo(player")
expect_rejected("IsDestinationActive guard behind GetBotAI(player)" "${e3}")

# 4. CheckStatus: the old group read on the raw member
string(REPLACE "ai->IsSafe(member) && SameMapObject(bot, member) && member->GetGroup()" "ai->IsSafe(member) && member->GetGroup()" probe_status_old "${status_fn}")
if("${probe_status_old}" STREQUAL "${status_fn}")
  message(FATAL_ERROR "${TAG}: probe 4 did not change the text (layout moved?)")
endif()
check_status_violations(e4 "${probe_status_old}")
expect_rejected("CheckStatus reads member->GetGroup() without the Map object test" "${e4}")

# 5. zone_keep loop: the old GetBotAI(member)
string(REPLACE "GetBotAIOnMapOf(bot, member)" "GetBotAI(member)" probe_zone_old "${zone_fn}")
if("${probe_zone_old}" STREQUAL "${zone_fn}")
  message(FATAL_ERROR "${TAG}: probe 5 did not change the text (layout moved?)")
endif()
zone_keep_violations(e5 "${probe_zone_old}")
expect_rejected("zone_keep loop reads a foreign AI" "${e5}")

message(STATUS "${TAG} contract passed")
