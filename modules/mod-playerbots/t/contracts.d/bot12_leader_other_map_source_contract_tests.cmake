cmake_policy(VERSION 3.16)
# BOT-12 (OB-00 04.11.2026; twow-repo#677, #628 third finding; cand4G u2: an ad-hoc member followed its leader on another
# continent for 10 h at ~13 ms per update and never left). Two switches, both default 0 = as before:
# - AiPlayerbot.BotGroups.LeaderOtherMap (0 off, 1 all, 2 cohort B, 3 cohort A):
#   (1) REMOVED (OB-00 04.11.2026: no group mutation on a map thread): the map-thread self-leave of the narrow patch
#       (LeaveGroupAction adhocLeave branch, ShouldStayInGroup(..., adhocLeave)) is gone - LeaveGroupAction and
#       SingletonGroupPolicy.h stay as on release (check_no_map_thread_leave). The self-leave is the world-thread group
#       queue's (AiPlayerbot.BotGroups.OpQueue, bot12_group_op_queue_source_contract_tests.cmake);
#   (2a) leave reason leader_other_map after LeaderOtherMapSeconds (120, 30..900), checked before out_of_range, in both leave
#       paths (action and world-thread pass), from their existing per-bot state - no new log line; the switch-0 wiring
#       (OutOfRangeFact with otherMapRule, the gated instance arm), the leader guard (leader != bot), the tested clock
#       (LeaderOtherMapClock: a leader not in the world keeps it) and its reset on a group change are pinned per copy;
#   (2b) FollowAction::isUseful: the map check comes before the first distance (and before the rpg/free-move check).
# - AiPlayerbot.Travel.ElevatorScanCache (0/1): getTransports' elevator branch walks a per-map list of transport-type
#   spawns built once, not all gameobject spawn data on every call.
# Pure rules: t/adhoc_group_policy_tests.cpp. Every check_* returns "" when it holds,
# else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "bot12: PB_SOURCE_DIR is required")
endif()

set(TAG "bot12 leader_other_map")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

# The text from the first `start` up to the next `stop` after it ("" when start is missing).
function(region text start stop out_var)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(LENGTH "${start}" start_len)
  string(SUBSTRING "${rest}" ${start_len} -1 after)
  string(FIND "${after}" "${stop}" end)
  if(end EQUAL -1)
    set(${out_var} "${rest}" PARENT_SCOPE)
  else()
    math(EXPR len "${start_len} + ${end}")
    string(SUBSTRING "${rest}" 0 ${len} body)
    set(${out_var} "${body}" PARENT_SCOPE)
  endif()
endfunction()

# "" when `first` occurs in text and before every occurrence of `later` (later may be absent), else the reason.
function(check_before text first later what out)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${later}" b)
  if(a EQUAL -1)
    set(${out} "${what}: '${first}' missing" PARENT_SCOPE)
  elseif(NOT b EQUAL -1 AND NOT a LESS b)
    set(${out} "${what}: '${first}' is not before '${later}'" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

# ---- (2b) follow: the map check before the first distance ----------------------------------------------------------
function(check_follow follow out)
  region("${follow}" "bool FollowAction::isUseful()" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "FollowAction::isUseful not found" PARENT_SCOPE)
    return()
  endif()
  set(guard "if (followTarget && sPlayerbotAIConfig.botGroupsLeaderOtherMap)")
  foreach(later "GetDistance2d(" "IsDistanceGreaterThan(" "CanFreeMoveTo(" "GetChaseTarget(")
    check_before("${body}" "${guard}" "${later}" "map check before ${later}" r)
    if(NOT r STREQUAL "")
      set(${out} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "adhoc_group::OnOtherMap(followTarget->GetMapId() != bot->GetMapId(),"
      "bool const ruleOn = ai::cohort::Applies(sPlayerbotAIConfig.botGroupsLeaderOtherMap, bot->GetGUIDLow());"
      "if (ruleOn && adhoc_group::FollowUselessAcrossMaps(ruleOn, otherMap, ai->HasRealPlayerMaster(),"
      "IsRealPlayerTarget(followTarget)))\n                return false;")
    string(FIND "${body}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "follow map check: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  if(body MATCHES "sLog\\.out")
    set(${out} "a log line in FollowAction::isUseful (no per-update lines)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (2a) the reason, before out_of_range ---------------------------------------------------------------------------
function(check_policy policy out)
  region("${policy}" "inline Leave DecideLeave(LeaveFacts const& f)" "\n}\n" decide)
  check_before("${decide}"
    "if (f.leaderOtherMapSeconds && f.leaderOtherMapSince && f.now - f.leaderOtherMapSince >= f.leaderOtherMapSeconds)\n        return Leave::LeaderOtherMap;"
    "if (f.outOfRangeSince" "leader_other_map before out_of_range" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_before("${decide}" "return Leave::Instance;" "return Leave::LeaderOtherMap;" "instance before leader_other_map" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${policy}" "case Leave::LeaderOtherMap: return \"leader_other_map\";" at)
  if(at EQUAL -1)
    set(${out} "reason string leader_other_map missing" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (1) removed: no map-thread self-leave (OB-00 04.11.2026) ---------------------------------------------------------
# LeaveGroupAction keeps release's stay rule (a free bot asked by itself stays) and no LeaderOtherMap/adhocLeave branch;
# SingletonGroupPolicy.h has no adhocLeave overload. The OpQueue contract pins the world-thread leave that replaces it.
function(check_no_map_thread_leave leave singleton out)
  foreach(forbidden "adhocLeave" "botGroupsLeaderOtherMap" "LeaveGroup(")
    string(FIND "${leave}" "${forbidden}" at)
    if(NOT at EQUAL -1)
      set(${out} "LeaveGroupAction: the map-thread ad-hoc leave is back (${forbidden})" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${singleton}" "adhocLeave" at)
  if(NOT at EQUAL -1)
    set(${out} "SingletonGroupPolicy.h: the adhocLeave overload is back" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${leave}" "bool shouldStay = singleton_group::ShouldStayInGroup(freeBot, bot->GetGroup() != nullptr, player == bot, selfLedSingleton);" at)
  if(at EQUAL -1)
    set(${out} "LeaveGroupAction: release's stay rule changed" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (3) getTransports uses the cache with switch B on -------------------------------------------------------------
function(check_transports wp out)
  region("${wp}" "std::set<GenericTransport*> WorldPosition::getTransports(uint32 entry)" "\nvoid WorldPosition::" body)
  check_before("${body}" "if (sPlayerbotAIConfig.travelElevatorScanCache && *this)" "getGameObjectsNear(" "cache branch before the full walk" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  region("${body}" "if (sPlayerbotAIConfig.travelElevatorScanCache && *this)" "\n        }\n" cached)
  foreach(needle "ElevatorSpawnsOf(getMapId())" "if (!entry || spawn.entry == entry)"
      "ObjectGuid(HIGHGUID_GAMEOBJECT, spawn.entry, spawn.guid)" "dynamic_cast<GenericTransport*>(go)" "return transports;")
    string(FIND "${cached}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "cached elevator branch: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(forbidden "DoGOData" "getGameObjectsNear(")
    string(FIND "${cached}" "${forbidden}" at)
    if(NOT at EQUAL -1)
      set(${out} "cached elevator branch walks the spawn data: ${forbidden}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "if (info && (info->type == GAMEOBJECT_TYPE_TRANSPORT || info->type == GAMEOBJECT_TYPE_MO_TRANSPORT))"
      "static ElevatorSpawnIndex const byMap = BuildElevatorSpawns();")
    string(FIND "${wp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "elevator spawn list: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (2a) the switch-0 wiring and the leader guard in a CheckLeave copy (OB-10 review) ---------------------------------
# ns: "adhoc_group::" in the action, "" in the world pass; reset_start/reset_needle: the group-change block and its reset.
function(check_leave_facts src ns clock reset_start reset_needle out)
  foreach(needle
      # The leader never runs the clock for itself, and no leader (not in the world) is no other-map fact.
      "bool const otherMap = leader && leader != bot && ${ns}OnOtherMap(leader->GetMapId() != bot->GetMapId(),"
      # Another instance counts only with the rule on (switch 0: the old map-id test alone).
      "otherMapRule && leader->GetInstanceId() != bot->GetInstanceId(), bot->GetMap()->Instanceable());"
      "bool const outOfRange = leader && leader != bot && ${ns}OutOfRangeFact(otherMap,"
      # Switch 0: OutOfRangeFact(otherMap, d, false) == otherMap || d, the old expression.
      "!otherMap && sServerFacade.GetDistance2d(bot, leader) > 2.0f * radius, otherMapRule);"
      "facts.leaderOtherMapSeconds = otherMapRule ? sPlayerbotAIConfig.botGroupsLeaderOtherMapSeconds : 0;"
      "${clock}")
    string(FIND "${src}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "CheckLeave wiring: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  region("${src}" "${reset_start}" "\n    }\n" reset)
  string(FIND "${reset}" "${reset_needle}" at)
  if(at EQUAL -1)
    set(${out} "CheckLeave: the group change does not reset the clock (${reset_needle})" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

set(ACTION_CLOCK "leaderOtherMapSince = adhoc_group::LeaderOtherMapClock(otherMapRule, leader != nullptr, otherMap, leaderOtherMapSince, now);")
set(WORLD_CLOCK "track.leaderOtherMapSince = LeaderOtherMapClock(otherMapRule, leader != nullptr, otherMap, track.leaderOtherMapSince, now);")
set(ACTION_RESET_START "    if (trackedGroup != group->GetId())")
# region() ends before the block's closing "\n    }\n", so the last reset line has no newline of its own there.
set(ACTION_RESET "\n        leaderOtherMapSince = 0;")
set(WORLD_RESET_START "    if (track.group != group->GetId())")
set(WORLD_RESET "\n        track = Track();")

read_source("strategy/actions/FollowActions.cpp" follow)
read_source("AdhocGroupPolicy.h" policy)
read_source("SingletonGroupPolicy.h" singleton)
read_source("strategy/actions/LeaveGroupAction.cpp" leave)
read_source("strategy/actions/AdhocGroupAction.cpp" action)
read_source("AdhocGroupWorld.cpp" world)
read_source("AdhocGroupWorld.h" world_h)
read_source("WorldPosition.cpp" wp)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

check_follow("${follow}" r)
fail_if("${r}")
check_policy("${policy}" r)
fail_if("${r}")
check_no_map_thread_leave("${leave}" "${singleton}" r)
fail_if("${r}")
check_transports("${wp}" r)
fail_if("${r}")

# (2a) both leave paths feed the clock from their existing per-bot state; the action keeps its core leave call and line.
foreach(src action world)
  require_text("${${src}}" "ai::cohort::Applies(sPlayerbotAIConfig.botGroupsLeaderOtherMap, bot->GetGUIDLow());" "${src}: the rule per bot")
  require_text("${${src}}" "facts.leaderOtherMapSeconds = otherMapRule ? sPlayerbotAIConfig.botGroupsLeaderOtherMapSeconds : 0;" "${src}: the switch reaches the facts")
  require_text("${${src}}" "OutOfRangeFact(otherMap," "${src}: out_of_range from the tested fact")
endforeach()
check_leave_facts("${action}" "adhoc_group::" "${ACTION_CLOCK}" "${ACTION_RESET_START}" "${ACTION_RESET}" r)
fail_if("${r}")
check_leave_facts("${world}" "" "${WORLD_CLOCK}" "${WORLD_RESET_START}" "${WORLD_RESET}" r)
fail_if("${r}")
require_text("${world_h}" "std::uint32_t leaderOtherMapSince = 0;" "world Track: the clock starts at 0 (track = Track() resets it)")
require_text("${policy}" "inline std::uint32_t LeaderOtherMapClock(bool ruleOn, bool leaderSeen, bool otherMap, std::uint32_t since, std::uint32_t now)\n{\n    if (!ruleOn)\n        return 0;" "the clock is 0 with the rule off")
require_text("${action}" "ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);" "the action's leave call with OpQueue off (release behaviour: a free bot stays)")
require_text("${action}" "[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u\"" "the existing leave line carries the reason")

# Config: keys, defaults, clamps, echo, documented with default 0.
require_text("${config_cpp}" "botGroupsLeaderOtherMap = config.GetIntDefault(\"AiPlayerbot.BotGroups.LeaderOtherMap\", 0);" "LeaderOtherMap key, default 0")
require_text("${config_cpp}" "if (botGroupsLeaderOtherMap > 3)" "LeaderOtherMap mode clamp (3 = cohort A)")
require_text("${config_cpp}" "botGroupsLeaderOtherMapSeconds = std::min<uint32>(900, uint32(std::max<int32>(30, config.GetIntDefault(\"AiPlayerbot.BotGroups.LeaderOtherMapSeconds\", 120))));" "LeaderOtherMapSeconds 120, 30..900")
require_text("${config_cpp}" "travelElevatorScanCache = config.GetBoolDefault(\"AiPlayerbot.Travel.ElevatorScanCache\", false);" "ElevatorScanCache key, default 0")
require_text("${config_cpp}" "{\"BotGroups.LeaderOtherMap\", Value(botGroupsLeaderOtherMap)}," "LeaderOtherMap echoed")
require_text("${config_cpp}" "{\"BotGroups.LeaderOtherMapSeconds\", Value(botGroupsLeaderOtherMapSeconds)}," "LeaderOtherMapSeconds echoed")
require_text("${config_cpp}" "{\"Travel.ElevatorScanCache\", Value(travelElevatorScanCache)}," "ElevatorScanCache echoed")
require_text("${config_h}" "uint32 botGroupsLeaderOtherMap = 0;" "LeaderOtherMap member default 0")
require_text("${config_h}" "uint32 botGroupsLeaderOtherMapSeconds = 120;" "LeaderOtherMapSeconds member default 120")
require_text("${config_h}" "bool travelElevatorScanCache = false;" "ElevatorScanCache member default 0")
require_text("${conf_dist}" "\nAiPlayerbot.BotGroups.LeaderOtherMap = 0\n" "LeaderOtherMap documented, 0")
require_text("${conf_dist}" "\nAiPlayerbot.BotGroups.LeaderOtherMapSeconds = 120\n" "LeaderOtherMapSeconds documented, 120")
require_text("${conf_dist}" "\nAiPlayerbot.Travel.ElevatorScanCache = 0\n" "ElevatorScanCache documented, 0")
require_text("${conf_dist}" "3 = only cohort A (even low guid; crossover unit 2)" "LeaderOtherMap mode 3 documented")

# ---- negative probes: each check catches its defect ------------------------------------------------------------------
function(expect_caught what reason)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

# The map check moved behind the first distance.
string(REPLACE "    if (followTarget && sPlayerbotAIConfig.botGroupsLeaderOtherMap)" "    distance = sServerFacade.GetDistance2d(bot, followTarget);\n    if (followTarget && sPlayerbotAIConfig.botGroupsLeaderOtherMap)" probe "${follow}")
check_follow("${probe}" r)
expect_caught("map check after a distance" "${r}")
# The map check without the real-player exception.
string(REPLACE "IsRealPlayerTarget(followTarget)))" "false))" probe "${follow}")
check_follow("${probe}" r)
expect_caught("real players no longer excepted" "${r}")
# The map check without the real-player-master exception.
string(REPLACE "ai->HasRealPlayerMaster()," "false," probe "${follow}")
check_follow("${probe}" r)
expect_caught("real-player masters no longer excepted" "${r}")
# Switch 0 wiring: OutOfRangeFact forced to the switch-on rule (switch 0 would lose the 60 s cross-map out_of_range).
foreach(src action world)
  string(REPLACE ", otherMapRule);" ", true);" probe "${${src}}")
  if(src STREQUAL "action")
    check_leave_facts("${probe}" "adhoc_group::" "${ACTION_CLOCK}" "${ACTION_RESET_START}" "${ACTION_RESET}" r)
  else()
    check_leave_facts("${probe}" "" "${WORLD_CLOCK}" "${WORLD_RESET_START}" "${WORLD_RESET}" r)
  endif()
  expect_caught("${src}: OutOfRangeFact without otherMapRule" "${r}")
endforeach()
# The leader guard dropped: the leader would run the clock for itself.
string(REPLACE "bool const otherMap = leader && leader != bot && adhoc_group::OnOtherMap(" "bool const otherMap = leader && adhoc_group::OnOtherMap(" probe "${action}")
check_leave_facts("${probe}" "adhoc_group::" "${ACTION_CLOCK}" "${ACTION_RESET_START}" "${ACTION_RESET}" r)
expect_caught("action: leader != bot dropped" "${r}")
string(REPLACE "bool const otherMap = leader && leader != bot && OnOtherMap(" "bool const otherMap = leader && OnOtherMap(" probe "${world}")
check_leave_facts("${probe}" "" "${WORLD_CLOCK}" "${WORLD_RESET_START}" "${WORLD_RESET}" r)
expect_caught("world: leader != bot dropped" "${r}")
# The instance arm without the rule gate (switch 0 would count another continent region).
string(REPLACE "otherMapRule && leader->GetInstanceId()" "leader->GetInstanceId()" probe "${world}")
check_leave_facts("${probe}" "" "${WORLD_CLOCK}" "${WORLD_RESET_START}" "${WORLD_RESET}" r)
expect_caught("world: instance arm without the rule" "${r}")
# The group change without the clock reset (an old since-time would fire at once in the new group).
string(REPLACE "        levelWindowSince = 0;\n        leaderOtherMapSince = 0;\n" "        levelWindowSince = 0;\n" probe "${action}")
check_leave_facts("${probe}" "adhoc_group::" "${ACTION_CLOCK}" "${ACTION_RESET_START}" "${ACTION_RESET}" r)
expect_caught("action: group change without the clock reset" "${r}")
# leader_other_map behind out_of_range.
string(REPLACE "    if (f.leaderOtherMapSeconds && f.leaderOtherMapSince && f.now - f.leaderOtherMapSince >= f.leaderOtherMapSeconds)\n        return Leave::LeaderOtherMap;\n" "" probe "${policy}")
string(REPLACE "        return Leave::OutOfRange;\n" "        return Leave::OutOfRange;\n    if (f.leaderOtherMapSeconds && f.leaderOtherMapSince && f.now - f.leaderOtherMapSince >= f.leaderOtherMapSeconds)\n        return Leave::LeaderOtherMap;\n" probe "${probe}")
check_policy("${probe}" r)
expect_caught("leader_other_map after out_of_range" "${r}")
# The map-thread ad-hoc leave brought back.
string(REPLACE "player == bot, selfLedSingleton);" "player == bot, selfLedSingleton, adhocLeave);" probe "${leave}")
check_no_map_thread_leave("${probe}" "${singleton}" r)
expect_caught("map-thread adhocLeave in LeaveGroupAction" "${r}")
string(APPEND probe_s "${singleton}" "\ninline bool ShouldStayInGroup(bool a, bool b, bool c, bool d, bool adhocLeave) { return !adhocLeave; }\n")
check_no_map_thread_leave("${leave}" "${probe_s}" r)
expect_caught("adhocLeave overload in SingletonGroupPolicy.h" "${r}")
# The cached branch falling through to the full walk.
string(REPLACE "                                transports.insert(transport);\n            return transports;\n" "                                transports.insert(transport);\n" probe "${wp}")
check_transports("${probe}" r)
expect_caught("cached branch without its return" "${r}")

message(STATUS "BOT12_LEADER_OTHER_MAP_SOURCE_CONTRACT=PASS")
