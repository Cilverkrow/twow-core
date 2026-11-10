cmake_policy(VERSION 3.16)
# twow-repo#708 follow-up (OB-00 10.11.2026; answer CLI-GRP 17f7c719, review OB-10 ccf5ba90): the three world-thread group managers are
# reached only from the roster branch of RandomPlayerbotMgr::UpdateAIInternal, and that is reached only from the world script hook.
# Why it matters: the managers read the bots' quest maps, AI contexts and groups from the world thread; that is safe only because the hook runs
# after sMapMgr.Update returned (tick order, pinned in the core contract t/check_tick_order_708_contract.cmake) - so a manager Update call
# from anywhere else (a bot thread, a packet handler, a script that runs inside the map update) would be a #697-class race.
# This contract pins:
#   - quest_groups / adhoc_group / group_mgr WorldManager().Update(GetAllBots(), ...) each exist exactly once in the module, in
#     RandomPlayerbotMgr.cpp, inside RandomPlayerbotMgr::UpdateAIInternal between LoginFreeBots() and PlayerbotHolder::UpdateAIInternal
#     (the roster branch);
#   - sRandomPlayerbotMgr.UpdateAI( has call sites only in PlayerbotScripts.cpp, inside PlayerbotWorldScript::OnUpdate (the WorldScript hook).
# Not repeated (pinned by adhoc_group_world_thread_source_contract, CLI-GRP #628, untouched): sMapMgr.Update before the hook,
# continents.wait() before asyncMapUpdating = false, sRandomPlayerbotMgr.UpdateAI(diff); present in the script.
# Negative probes: the same checkers must reject a moved, a duplicated and a foreign call site.

set(TAG "world managers in the hook (#708)")

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

function(count_text out text needle)
  string(REPLACE "${needle}" "@@MGRHIT@@" marked "${text}")
  string(REGEX MATCHALL "@@MGRHIT@@" hits "${marked}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# Cuts [start_marker, end_marker) out of a text; sets ${out}_found to FALSE when a marker is missing.
function(region out text start_marker end_marker)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    set(${out}_found FALSE PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    set(${out}_found FALSE PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
  set(${out}_found TRUE PARENT_SCOPE)
endfunction()

# no trailing semicolon in the needles: a ";" would split the CMake list
set(MANAGER_CALLS
  "ai::quest_groups::WorldManager().Update(GetAllBots(), uint32(time(nullptr)))"
  "ai::adhoc_group::WorldManager().Update(GetAllBots(), uint32(time(nullptr)))"
  "ai::group_mgr::WorldManager().Update(GetAllBots(), uint32(time(nullptr)))")

# The roster branch of RandomPlayerbotMgr.cpp holds each manager call exactly once, between LoginFreeBots() and the base UpdateAIInternal call.
function(manager_violations out rpm_text)
  set(errors "")
  region(branch "${rpm_text}" "        LoginFreeBots();" "        PlayerbotHolder::UpdateAIInternal(elapsed, minimal);")
  if(NOT branch_found)
    list(APPEND errors "roster branch (LoginFreeBots() .. PlayerbotHolder::UpdateAIInternal) not found")
  else()
    foreach(call ${MANAGER_CALLS})
      count_text(in_branch "${branch}" "${call}")
      count_text(in_file "${rpm_text}" "${call}")
      if(NOT in_branch EQUAL 1)
        list(APPEND errors "'${call}' must appear exactly once in the roster branch, found ${in_branch}")
      endif()
      if(NOT in_file EQUAL 1)
        list(APPEND errors "'${call}' must appear exactly once in RandomPlayerbotMgr.cpp, found ${in_file}")
      endif()
    endforeach()
  endif()
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

# UpdateAI( of the bot manager is called only inside PlayerbotWorldScript::OnUpdate; `other_files_hits` is the number of call sites found in
# any other file of the module.
function(update_ai_violations out script_text other_files_hits)
  set(errors "")
  count_text(total "${script_text}" "sRandomPlayerbotMgr.UpdateAI(")
  region(on_update "${script_text}" "        void OnUpdate(uint32 diff) override" "        void OnUpdate(Player")
  if(NOT on_update_found)
    # the next hook after OnUpdate(uint32) is not Player-based in every layout: fall back to the class end marker
    region(on_update "${script_text}" "        void OnUpdate(uint32 diff) override" "\n};")
  endif()
  if(NOT on_update_found)
    list(APPEND errors "PlayerbotWorldScript::OnUpdate(uint32) not found")
  else()
    count_text(in_hook "${on_update}" "sRandomPlayerbotMgr.UpdateAI(")
    if(in_hook LESS 1)
      list(APPEND errors "sRandomPlayerbotMgr.UpdateAI( is not called inside PlayerbotWorldScript::OnUpdate")
    endif()
    if(NOT in_hook EQUAL total)
      list(APPEND errors "sRandomPlayerbotMgr.UpdateAI( is called ${total} times in PlayerbotScripts.cpp but only ${in_hook} inside OnUpdate(uint32)")
    endif()
  endif()
  if(NOT other_files_hits EQUAL 0)
    list(APPEND errors "sRandomPlayerbotMgr.UpdateAI( called from ${other_files_hits} other site(s) in the module")
  endif()
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

read_source("RandomPlayerbotMgr.cpp" rpm)
read_source("PlayerbotScripts.cpp" scripts)

# call sites in the whole module (and mod-dungeon-clear) outside the two files above
file(GLOB_RECURSE all_sources
  "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h"
  "${PB_SOURCE_DIR}/../../../mod-dungeon-clear/src/*.cpp" "${PB_SOURCE_DIR}/../../../mod-dungeon-clear/src/*.h")
list(LENGTH all_sources n_sources)
if(n_sources LESS 100)
  message(FATAL_ERROR "${TAG}: source glob found only ${n_sources} files")
endif()
set(foreign_manager_sites 0)
set(foreign_update_ai_sites 0)
foreach(src ${all_sources})
  get_filename_component(src_name "${src}" NAME)
  file(READ "${src}" body)
  if(NOT src_name STREQUAL "RandomPlayerbotMgr.cpp")
    count_text(hits "${body}" "WorldManager().Update(GetAllBots()")
    math(EXPR foreign_manager_sites "${foreign_manager_sites} + ${hits}")
  endif()
  if(NOT src_name STREQUAL "PlayerbotScripts.cpp" AND NOT src_name STREQUAL "RandomPlayerbotMgr.cpp")
    count_text(update_hits "${body}" "sRandomPlayerbotMgr.UpdateAI(")
    math(EXPR foreign_update_ai_sites "${foreign_update_ai_sites} + ${update_hits}")
  endif()
endforeach()

# --- the real sources ---
manager_violations(real_manager_errors "${rpm}")
if(real_manager_errors)
  string(REPLACE ";" "\n  - " listed "${real_manager_errors}")
  message(FATAL_ERROR "${TAG}: broken:\n  - ${listed}")
endif()
if(NOT foreign_manager_sites EQUAL 0)
  message(FATAL_ERROR "${TAG}: a WorldManager().Update(GetAllBots() call exists in ${foreign_manager_sites} place(s) outside RandomPlayerbotMgr.cpp")
endif()
update_ai_violations(real_hook_errors "${scripts}" "${foreign_update_ai_sites}")
if(real_hook_errors)
  string(REPLACE ";" "\n  - " listed "${real_hook_errors}")
  message(FATAL_ERROR "${TAG}: broken:\n  - ${listed}")
endif()

# --- negative probes: moved, duplicated and foreign call sites must be rejected ---
function(expect_rejected name errs)
  if(NOT errs)
    message(FATAL_ERROR "${TAG}: negative probe '${name}' was accepted - the checker no longer sees it")
  endif()
endfunction()

list(GET MANAGER_CALLS 0 quest_call)
list(GET MANAGER_CALLS 1 adhoc_call)

# 1. a manager call moved behind the base UpdateAIInternal call (out of the roster branch)
string(REPLACE "            ${quest_call}" "            /* moved */" probe_moved_tmp "${rpm}")
string(REPLACE "        PlayerbotHolder::UpdateAIInternal(elapsed, minimal);" "        PlayerbotHolder::UpdateAIInternal(elapsed, minimal);\n        ${quest_call}" probe_moved "${probe_moved_tmp}")
manager_violations(e1 "${probe_moved}")
expect_rejected("manager call moved out of the roster branch" "${e1}")

# 2. a duplicated manager call
string(REPLACE "            ${adhoc_call}" "            ${adhoc_call}\n            ${adhoc_call}" probe_dup "${rpm}")
manager_violations(e2 "${probe_dup}")
expect_rejected("manager call duplicated" "${e2}")

# 3. a manager call removed
string(REPLACE "            ${quest_call}" "            /* removed */" probe_removed "${rpm}")
manager_violations(e3 "${probe_removed}")
expect_rejected("manager call removed" "${e3}")

# 4. UpdateAI called from outside the world script hook (another file of the module)
update_ai_violations(e4 "${scripts}" 1)
expect_rejected("UpdateAI called from another file" "${e4}")

# 5. UpdateAI called in the script outside OnUpdate(uint32)
set(probe_script "${scripts}\n        void SomethingElse() { sRandomPlayerbotMgr.UpdateAI(1); }\n};")
update_ai_violations(e5 "${probe_script}" 0)
expect_rejected("UpdateAI called outside OnUpdate in the script" "${e5}")

message(STATUS "${TAG} contract passed")
