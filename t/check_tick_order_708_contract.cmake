cmake_policy(VERSION 3.16)

if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#708 (answer CLI-GRP 17f7c719, review OB-10 ccf5ba90; order OB-00 10.11.2026), contract only, no runtime change:
# the tick order that makes the world-thread bot managers safe by construction.
#
# World::Update runs sMapMgr.Update(diff) and only then, in the same tick, the WORLDHOOK_ON_UPDATE hook. MapManager::Update returns only after every
# instance pool job (f.wait()) and the continent regions (continents.wait()) have finished, and only then does the world thread alone run
# SwitchPlayersInstances() and clear asyncMapUpdating. So when the hook runs, no region or instance thread is updating a bot: the arbiter shadow,
# the #676 far-teleport ack drain, the BOT-12 group_ops::Drain and the A3 group managers (AdhocGroupWorld, QuestGroupWorld, GroupManager: reads of
# the bots' quest maps, AI contexts and groups) read single-threaded. A silent reorder would turn all of them into #697-class races at once.
#
# Already pinned elsewhere and NOT repeated here (no double pin): the mod-playerbots contract adhoc_group_world_thread_source_contract
# (sMapMgr.Update before the hook; continents.wait() before asyncMapUpdating = false; sRandomPlayerbotMgr.UpdateAI(diff) in the world script).
# This one adds the instance pool wait, the order against SwitchPlayersInstances, the single hook call site and the async job wait.
#
# Negative probes: the same checker is run on reordered copies of the real text and must reject every one of them.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Cuts the body between two markers (start inclusive) out of a source text.
function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if (length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# ok = TRUE when `first` is present and comes strictly before `second` (both present); problems accumulate in the list variable `errors`.
function(check_order errors_var text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  set(errors "${${errors_var}}")
  if (a EQUAL -1)
    list(APPEND errors "missing '${first}' (${description})")
  elseif (b EQUAL -1)
    list(APPEND errors "missing '${second}' (${description})")
  elseif (NOT a LESS b)
    list(APPEND errors "order (${description}): '${first}' must come before '${second}'")
  endif()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

function(count_text out text needle)
  string(REPLACE "${needle}" "@@TICKHIT@@" marked "${text}")
  string(REGEX MATCHALL "@@TICKHIT@@" hits "${marked}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# The tick order checker: returns the list of violations (empty = the order holds).
#   world_text: src/game/World.cpp, map_text: src/game/Maps/MapManager.cpp
function(tick_order_violations out world_text map_text)
  set(errors "")

  # --- World::Update: the map update, then the async job wait, then the only hook call ---
  check_order(errors "${world_text}" "    sMapMgr.Update(diff);" "ForEachEnabledHook(WORLDHOOK_ON_UPDATE" "the world script hook runs after the map update")
  check_order(errors "${world_text}" "        job.wait();" "ForEachEnabledHook(WORLDHOOK_ON_UPDATE" "the world async job is waited before the hook")
  count_text(hook_calls "${world_text}" "ForEachEnabledHook(WORLDHOOK_ON_UPDATE")
  if (NOT hook_calls EQUAL 1)
    list(APPEND errors "World.cpp must contain exactly one WORLDHOOK_ON_UPDATE call, found ${hook_calls}")
  endif()
  # OB-10 review of #1004/#1005: the two order anchors must be unique in the file, otherwise an unrelated earlier occurrence
  # (a comment, another function) would satisfy the order check without the real call being before the hook.
  count_text(map_update_calls "${world_text}" "    sMapMgr.Update(diff);")
  if (NOT map_update_calls EQUAL 1)
    list(APPEND errors "World.cpp must contain exactly one 'sMapMgr.Update(diff);', found ${map_update_calls}")
  endif()
  count_text(job_waits "${world_text}" "        job.wait();")
  if (NOT job_waits EQUAL 1)
    list(APPEND errors "World.cpp must contain exactly one 'job.wait();', found ${job_waits}")
  endif()

  # --- MapManager::Update: instance pool wait, continents wait, only then the world thread alone switches players ---
  string(FIND "${map_text}" "void MapManager::Update(uint32 diff)" has_update)
  if (has_update EQUAL -1)
    list(APPEND errors "MapManager::Update(uint32 diff) not found")
  else()
    region("${map_text}" "void MapManager::Update(uint32 diff)" "asyncMapUpdating = false;" update_head)
    set(update_body "${update_head}asyncMapUpdating = false;")   # the end marker belongs to the window being checked
    check_order(errors "${update_body}" "asyncMapUpdating = true;" "processWorkload(instancesUpdaters" "the instance jobs start inside the asyncMapUpdating window")
    check_order(errors "${update_body}" "            f.wait();" "        continents.wait();" "the instance pool is waited before the continents")
    check_order(errors "${update_body}" "        continents.wait();" "    SwitchPlayersInstances();" "the continents are waited before SwitchPlayersInstances")
    check_order(errors "${update_body}" "            f.wait();" "    SwitchPlayersInstances();" "the instance pool is waited before SwitchPlayersInstances")
    check_order(errors "${update_body}" "    SwitchPlayersInstances();" "    asyncMapUpdating = false;" "players are switched inside the asyncMapUpdating window")
    check_order(errors "${update_body}" "    if (continents.valid())\n        continents.wait();" "    SwitchPlayersInstances();" "the continents wait is unconditional when the future is valid")
    count_text(f_waits "${update_body}" "f.wait();")
    if (NOT f_waits EQUAL 1)
      list(APPEND errors "MapManager::Update must wait the instance pool exactly once per round (f.wait();), found ${f_waits}")
    endif()
    count_text(cont_waits "${update_body}" "continents.wait();")
    if (NOT cont_waits EQUAL 1)
      list(APPEND errors "MapManager::Update must wait the continents exactly once (continents.wait();), found ${cont_waits}")
    endif()
  endif()

  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

read_source("src/game/World.cpp" world)
read_source("src/game/Maps/MapManager.cpp" mapmgr)

# the hook has exactly one call site in the whole source tree (a second one would run outside the barrier)
file(GLOB_RECURSE all_sources "${TW_CORE_ROOT}/src/*.cpp" "${TW_CORE_ROOT}/src/*.h")
list(LENGTH all_sources n_sources)
if (n_sources LESS 500)
  message(FATAL_ERROR "tick order: source glob found only ${n_sources} files")
endif()
set(hook_sites 0)
foreach(src ${all_sources})
  file(READ "${src}" body)
  string(FIND "${body}" "ForEachEnabledHook(WORLDHOOK_ON_UPDATE" pos)
  if (NOT pos EQUAL -1)
    math(EXPR hook_sites "${hook_sites} + 1")
  endif()
endforeach()
if (NOT hook_sites EQUAL 1)
  message(FATAL_ERROR "tick order: WORLDHOOK_ON_UPDATE must be called from exactly one file (World.cpp), found ${hook_sites}")
endif()

# --- the real sources keep the order ---
tick_order_violations(real_errors "${world}" "${mapmgr}")
if (real_errors)
  string(REPLACE ";" "\n  - " listed "${real_errors}")
  message(FATAL_ERROR "tick order (twow-repo#708) broken:\n  - ${listed}")
endif()

# --- negative probes: reordered copies of the real text must be rejected ---
function(expect_rejected name world_text map_text)
  tick_order_violations(errs "${world_text}" "${map_text}")
  if (NOT errs)
    message(FATAL_ERROR "tick order: negative probe '${name}' was accepted - the checker no longer sees this reordering")
  endif()
endfunction()

# 1. the hook before the map update
string(REPLACE "    sMapMgr.Update(diff);" "    ForEachEnabledHook(WORLDHOOK_ON_UPDATE\n    sMapMgr.Update(diff);" probe_world_1 "${world}")
expect_rejected("hook before sMapMgr.Update" "${probe_world_1}" "${mapmgr}")

# 2. a second hook call site in World.cpp
set(probe_world_2 "${world}\n    ScriptRegistry<WorldScript>::ForEachEnabledHook(WORLDHOOK_ON_UPDATE, x);\n")
expect_rejected("second hook call" "${probe_world_2}" "${mapmgr}")

# 3. the async job no longer waited before the hook
string(REPLACE "        job.wait();" "        /* job.wait(); */" probe_world_3 "${world}")
expect_rejected("job wait removed" "${probe_world_3}" "${mapmgr}")

# 3b. (OB-10) an unrelated EARLIER occurrence of an order anchor must not satisfy the check: a second 'sMapMgr.Update(diff);' before the hook
# (as if copied into another function above) and a second 'job.wait();' are rejected by the uniqueness count
string(REPLACE "    sMapMgr.Update(diff);" "    sMapMgr.Update(diff);\n    sMapMgr.Update(diff);" probe_world_3b "${world}")
expect_rejected("second sMapMgr.Update(diff) anchor" "${probe_world_3b}" "${mapmgr}")
string(REPLACE "        job.wait();" "        job.wait();\n        job.wait();" probe_world_3c "${world}")
expect_rejected("second job.wait anchor" "${probe_world_3c}" "${mapmgr}")

# 4. SwitchPlayersInstances before continents.wait()
string(REPLACE "    SwitchPlayersInstances();" "    SwitchPlayersInstances_MOVED();" probe_map_tmp "${mapmgr}")
string(REPLACE "        continents.wait();" "    SwitchPlayersInstances();\n        continents.wait();" probe_map_4 "${probe_map_tmp}")
expect_rejected("SwitchPlayersInstances before continents.wait" "${world}" "${probe_map_4}")

# 5. the instance pool wait replaced (f.get() is no accepted spelling of the pinned barrier)
string(REPLACE "            f.wait();" "            f.get();" probe_map_5 "${mapmgr}")
expect_rejected("f.wait replaced" "${world}" "${probe_map_5}")

# 6. the continents wait made conditional on something else / removed
string(REPLACE "    if (continents.valid())\n        continents.wait();" "    if (continents.valid() && someFlag)\n        continents.wait();" probe_map_6 "${mapmgr}")
expect_rejected("continents wait made conditional" "${world}" "${probe_map_6}")

# 7. the instance wait after the continents wait
string(REPLACE "            f.wait();" "            /* waited later */" probe_map_tmp7 "${mapmgr}")
string(REPLACE "        continents.wait();" "        continents.wait();\n            f.wait();" probe_map_7 "${probe_map_tmp7}")
expect_rejected("instance wait after continents wait" "${world}" "${probe_map_7}")

# 8. asyncMapUpdating cleared before the players are switched
string(REPLACE "    asyncMapUpdating = false;" "    asyncMapUpdating = false_MOVED;" probe_map_tmp8 "${mapmgr}")
string(REPLACE "    SwitchPlayersInstances();" "    asyncMapUpdating = false;\n    SwitchPlayersInstances();" probe_map_8 "${probe_map_tmp8}")
expect_rejected("asyncMapUpdating cleared before the switch" "${world}" "${probe_map_8}")

message(STATUS "tick order contract passed (twow-repo#708)")
