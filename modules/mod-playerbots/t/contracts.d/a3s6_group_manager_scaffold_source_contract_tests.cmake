cmake_policy(VERSION 3.16)
# A3 step 6 (ADR-0041, OB-00 06.11.2026): the group manager scaffold, AiPlayerbot.BotGroups.Manager (0 off = default, 1 all,
# 2 cohort B, 3 cohort A; other = 0 + error line). Design ws-10/cli-grp/a3-schritt6/ENTWURF-A3-SCHRITT6-GRUPPENMANAGER-20261106.md.
# The scaffold decides nothing. This contract pins that:
#   - the switch: member default 0, key read with default 0 and clamped by group_mgr::ValidMode, documented = 0, echoed;
#   - one call site: RandomPlayerbotMgr's roster branch (world thread, maps idle), gated by the switch, nowhere else;
#   - the pure header names no game header and reads no config;
#   - the manager touches no group: no group handler, no Group::*, no ObjectMgr group map, no group op, no strategy change, no
#     teleport. The pair that gives the manager its first behaviour changes this check on purpose, together with its own A/B.
#     The pass is the counter plus the inbox drain (A3-GERUEST-L60 7.2.11, measurement only; pinned in detail by
#     a3s6_manager_inbox_source_contract_tests.cmake).
# Pure rules: t/group_manager_policy_tests.cpp. Every check_* returns "" when it holds, else the reason; the negative probes at
# the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s6: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s6: PB_MODULE_DIR is required")
endif()

set(TAG "a3s6 group manager scaffold")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("GroupManagerPolicy.h" policy)
read_source("GroupManager.h" mgr_h)
read_source("GroupManager.cpp" mgr_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" rpm)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")

# All module sources, for the call-site count.
file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(APPEND all_sources "${t}\n")
endforeach()

# ---- the switch ----------------------------------------------------------------------------------------------------------
function(check_switch config_h config_cpp conf_dist out)
  foreach(needle "uint32 botGroupsManager = 0;")
    string(FIND "${config_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.h: member not default 0: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "botGroupsManager = uint32(config.GetIntDefault(\"AiPlayerbot.BotGroups.Manager\", 0));"
      "if (!ai::group_mgr::ValidMode(botGroupsManager))"
      "{\"BotGroups.Manager\", Value(botGroupsManager)},")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX MATCHALL "\nAiPlayerbot\\.BotGroups\\.Manager = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1)
    set(${out} "conf.dist: AiPlayerbot.BotGroups.Manager documented ${n} times (1 expected)" PARENT_SCOPE)
    return()
  endif()
  if(NOT lines STREQUAL "\nAiPlayerbot.BotGroups.Manager = 0")
    set(${out} "conf.dist: AiPlayerbot.BotGroups.Manager is not 0" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the one call site ---------------------------------------------------------------------------------------------------
function(check_call_site rpm all_sources out)
  # T30-1: also while the manager still holds a group after a switch to 0 (HasGroups), so it ends them through the op path.
  set(gate "        if (sPlayerbotAIConfig.botGroupsManager || ai::group_mgr::WorldManager().HasGroups())\n            ai::group_mgr::WorldManager().Update(GetAllBots(), uint32(time(nullptr)));\n        PlayerbotHolder::UpdateAIInternal(elapsed, minimal);")
  string(FIND "${rpm}" "${gate}" at)
  if(at EQUAL -1)
    set(${out} "RandomPlayerbotMgr.cpp: the gated call before PlayerbotHolder::UpdateAIInternal is missing" PARENT_SCOPE)
    return()
  endif()
  # In the persistent-roster branch: after the #628 helpers release, the world-thread manager position.
  string(FIND "${rpm}" "ai::adhoc_group::WorldManager().ReleaseHelpers(GetAllBots());" helpers)
  if(helpers EQUAL -1 OR helpers GREATER at)
    set(${out} "RandomPlayerbotMgr.cpp: the call is not after the #628 manager block" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "group_mgr::WorldManager\\(\\)\\.Update\\(" calls "${all_sources}")
  list(LENGTH calls n)
  if(NOT n EQUAL 1)
    set(${out} "group_mgr::WorldManager().Update( called ${n} times in the module (1 expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the pure header -----------------------------------------------------------------------------------------------------
function(check_pure policy out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${policy}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include (\"CohortPolicy\\.h\"|<cstddef>|<cstdint>)$")
      set(${out} "GroupManagerPolicy.h: not a pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(word "sPlayerbotAIConfig" "sObjectMgr" "sLog" "Player*" "Group*" "std::mutex")
    string(FIND "${policy}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "GroupManagerPolicy.h: names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- no direct group write -----------------------------------------------------------------------------------------------
# T30-1: the manager acts only through the BOT-12 manager queue (QueueManagerJoin / QueueManagerLeave / TakeManagerOutcomes)
# and logs through its one WorldIo line; every other group write or game call stays banned.
function(check_no_behaviour mgr_h mgr_cpp out)
  set(code "${mgr_h}\n${mgr_cpp}")
  set(log_line "    void Line(char const* line) override { sLog.outBasic(\"%s\", line); }")
  string(FIND "${mgr_cpp}" "${log_line}" at)
  if(at EQUAL -1)
    set(${out} "GroupManager.cpp: the one WorldIo log line is missing" PARENT_SCOPE)
    return()
  endif()
  string(REPLACE "${log_line}" "" code "${code}")
  foreach(allowed "group_ops::QueueManagerJoin(" "group_ops::QueueManagerLeave(" "group_ops::TakeManagerOutcomes("
      "group_ops::QueueManagerJoin /" "group_ops::ManagerOutcome>")
    string(REPLACE "${allowed}" "" code "${code}")
  endforeach()
  foreach(word
      "HandleGroup" "RemoveMember" "AddMember" "Disband" "AddGroup" "RemoveGroup" "RemoveFromGroup" "UninviteFromGroup"
      "->Create(" "ChangeLeader" "GetGroupById" "GetGroupByMember" "sObjectMgr" "group_ops::" "DoSpecificAction"
      "ChangeStrategy" "ResetStrategies" "RequestGroupAi" "TeleportTo" "SetForcedRole" "dungeon_run::" "sLog")
    string(FIND "${code}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "GroupManager: must not use ${word} (only the BOT-12 manager queue and the one log line)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${mgr_cpp}" "void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)\n{\n    ++passes_;\n    std::uint32_t const nowMs = WorldTimer::getMSTime();\n    DrainInbox(bots, nowMs);\n    RunDirectives(bots, nowMs, now);\n}" at)
  if(at EQUAL -1)
    set(${out} "GroupManager.cpp: the pass is not the counter, the inbox drain and the directive pass" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the unit test is registered -----------------------------------------------------------------------------------------
function(check_unit_test tests_cmake out)
  foreach(needle "add_executable(group_manager_policy_tests" "add_test(NAME group_manager_policy\n  COMMAND group_manager_policy_tests")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "tests.cmake: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- run on the real sources ---------------------------------------------------------------------------------------------
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_call_site("${rpm}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_pure("${policy}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_no_behaviour("${mgr_h}" "${mgr_cpp}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_unit_test("${tests_cmake}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------
function(expect_caught result description)
  if(result STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${description}")
  endif()
endfunction()

string(REPLACE "AiPlayerbot.BotGroups.Manager = 0" "AiPlayerbot.BotGroups.Manager = 1" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "conf.dist default 1")

string(REPLACE "uint32 botGroupsManager = 0;" "uint32 botGroupsManager = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.BotGroups.Manager\", 0)" "\"AiPlayerbot.BotGroups.Manager\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "if (!ai::group_mgr::ValidMode(botGroupsManager))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "        if (sPlayerbotAIConfig.botGroupsManager || ai::group_mgr::WorldManager().HasGroups())\n" "" m "${rpm}")
check_call_site("${m}" "${all_sources}" r)
expect_caught("${r}" "ungated call")

string(REPLACE "        if (sPlayerbotAIConfig.botGroupsManager || ai::group_mgr::WorldManager().HasGroups())\n" "        if (sPlayerbotAIConfig.botGroupsManager)\n" m "${rpm}")
check_call_site("${m}" "${all_sources}" r)
expect_caught("${r}" "held groups orphaned after a switch to 0")

string(REPLACE "    void Line(char const* line) override { sLog.outBasic(\"%s\", line); }" "    void Line(char const* line) override { sLog.outBasic(\"%s\", line); }\n    void Raw() { sLog.outError(\"x\"); }" m "${mgr_cpp}")
check_no_behaviour("${mgr_h}" "${m}" r)
expect_caught("${r}" "a second log site in the manager")

check_call_site("${rpm}" "${all_sources}\nai::group_mgr::WorldManager().Update(x, y);" r)
expect_caught("${r}" "second call site")

check_pure("${policy}\n#include \"playerbot/playerbot.h\"" r)
expect_caught("${r}" "game include in the pure header")

check_pure("${policy}\nbool x = sPlayerbotAIConfig.enabled;" r)
expect_caught("${r}" "config read in the pure header")

string(REPLACE "    ++passes_;\n" "    ++passes_;\n    bot->GetSession()->HandleGroupDisbandOpcode(packet);\n" m "${mgr_cpp}")
check_no_behaviour("${mgr_h}" "${m}" r)
expect_caught("${r}" "a group handler in the scaffold")

string(REPLACE "    ++passes_;\n" "    ++passes_;\n    ai::group_ops::Drain();\n" m "${mgr_cpp}")
check_no_behaviour("${mgr_h}" "${m}" r)
expect_caught("${r}" "a group op in the scaffold")

string(REPLACE "add_test(NAME group_manager_policy\n" "add_test(NAME other\n" m "${tests_cmake}")
check_unit_test("${m}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
