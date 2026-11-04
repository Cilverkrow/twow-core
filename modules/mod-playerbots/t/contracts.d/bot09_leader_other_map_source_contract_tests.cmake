cmake_policy(VERSION 3.16)
# BOT-09 (OB-00 04.11.2026; twow-repo#672, #628 third finding; cand4G u2: an ad-hoc member followed its leader on another
# continent for 10 h at ~13 ms per update and never left). Two switches, both default 0 = as before:
# - AiPlayerbot.BotGroups.LeaderOtherMap (0 off, 1 all, 2 cohort B, 3 cohort A):
#   (1) the ad-hoc leave of the action path really leaves for a free bot: LeaveGroupAction passes the "adhoc group" source
#       to the tested stay rule (singleton_group::ShouldStayInGroup with adhocLeave);
#   (2a) leave reason leader_other_map after LeaderOtherMapSeconds (120, 30..900), checked before out_of_range, in both leave
#       paths (action and world-thread pass), from their existing per-bot state - no new log line;
#   (2b) FollowAction::isUseful: the map check comes before the first distance (and before the rpg/free-move check).
# - AiPlayerbot.Travel.ElevatorScanCache (0/1): getTransports' elevator branch walks a per-map list of transport-type
#   spawns built once, not all gameobject spawn data on every call.
# Pure rules: t/adhoc_group_policy_tests.cpp, t/singleton_group_policy_tests.cpp. Every check_* returns "" when it holds,
# else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "bot09: PB_SOURCE_DIR is required")
endif()

set(TAG "bot09 leader_other_map")

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
      "if (otherMap && adhoc_group::FollowUselessAcrossMaps("
      "ai::cohort::Applies(sPlayerbotAIConfig.botGroupsLeaderOtherMap, bot->GetGUIDLow()), otherMap,"
      "IsRealPlayerTarget(followTarget)))\n            return false;")
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

# ---- (1) the leave consults the new stay rule ------------------------------------------------------------------------
function(check_leave leave out)
  region("${leave}" "bool LeaveGroupAction::Execute(Event& event)" "\n    }\n" exec)
  foreach(needle
      "bool const adhocLeave = master == bot && event.getSource() == \"adhoc group\" &&"
      "ai::cohort::Applies(sPlayerbotAIConfig.botGroupsLeaderOtherMap, bot->GetGUIDLow());"
      "return LeaveGroup(master, adhocLeave);")
    string(FIND "${exec}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "LeaveGroupAction::Execute: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "bool LeaveGroupAction::Leave(Player* player)\n    {\n        return LeaveGroup(player, false);\n    }"
      "bool shouldStay = singleton_group::ShouldStayInGroup(freeBot, bot->GetGroup() != nullptr, player == bot, selfLedSingleton, adhocLeave);")
    string(FIND "${leave}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "LeaveGroupAction: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
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

read_source("strategy/actions/FollowActions.cpp" follow)
read_source("AdhocGroupPolicy.h" policy)
read_source("SingletonGroupPolicy.h" singleton)
read_source("strategy/actions/LeaveGroupAction.cpp" leave)
read_source("strategy/actions/AdhocGroupAction.cpp" action)
read_source("AdhocGroupWorld.cpp" world)
read_source("WorldPosition.cpp" wp)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

check_follow("${follow}" r)
fail_if("${r}")
check_policy("${policy}" r)
fail_if("${r}")
check_leave("${leave}" r)
fail_if("${r}")
check_transports("${wp}" r)
fail_if("${r}")

require_text("${singleton}" "    return !adhocLeave && ShouldStayInGroup(freeBot, inGroup, requestedByBotItself, selfLedSingleton);" "the stay rule with adhocLeave")

# (2a) both leave paths feed the clock from their existing per-bot state; the action keeps its core leave call and line.
foreach(src action world)
  require_text("${${src}}" "ai::cohort::Applies(sPlayerbotAIConfig.botGroupsLeaderOtherMap, bot->GetGUIDLow());" "${src}: the rule per bot")
  require_text("${${src}}" "facts.leaderOtherMapSeconds = otherMapRule ? sPlayerbotAIConfig.botGroupsLeaderOtherMapSeconds : 0;" "${src}: the switch reaches the facts")
  require_text("${${src}}" "OutOfRangeFact(otherMap," "${src}: out_of_range from the tested fact")
endforeach()
require_text("${action}" "leaderOtherMapSince = otherMapRule && otherMap ? (leaderOtherMapSince ? leaderOtherMapSince : now) : 0;" "action clock")
require_text("${world}" "track.leaderOtherMapSince = otherMapRule && otherMap ? (track.leaderOtherMapSince ? track.leaderOtherMapSince : now) : 0;" "world-pass clock")
require_text("${action}" "ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);" "the action's leave call (its source is what LeaveGroupAction reads)")
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
# leader_other_map behind out_of_range.
string(REPLACE "    if (f.leaderOtherMapSeconds && f.leaderOtherMapSince && f.now - f.leaderOtherMapSince >= f.leaderOtherMapSeconds)\n        return Leave::LeaderOtherMap;\n" "" probe "${policy}")
string(REPLACE "        return Leave::OutOfRange;\n" "        return Leave::OutOfRange;\n    if (f.leaderOtherMapSeconds && f.leaderOtherMapSince && f.now - f.leaderOtherMapSince >= f.leaderOtherMapSeconds)\n        return Leave::LeaderOtherMap;\n" probe "${probe}")
check_policy("${probe}" r)
expect_caught("leader_other_map after out_of_range" "${r}")
# The leave without the new rule.
string(REPLACE "player == bot, selfLedSingleton, adhocLeave);" "player == bot, selfLedSingleton);" probe "${leave}")
check_leave("${probe}" r)
expect_caught("stay rule without adhocLeave" "${r}")
# The cached branch falling through to the full walk.
string(REPLACE "                                transports.insert(transport);\n            return transports;\n" "                                transports.insert(transport);\n" probe "${wp}")
check_transports("${probe}" r)
expect_caught("cached branch without its return" "${r}")

message(STATUS "BOT09_LEADER_OTHER_MAP_SOURCE_CONTRACT=PASS")
