cmake_policy(VERSION 3.16)
# A3-GERUEST-L60 v1.1 8.4 lever 3 (OB-00 07.11.2026; QB-BUILD-PLAN-20261107.md section 8): the stale objective query without a lock,
# AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree (0 = the per-thread rebuild under the store mutex, default and
# rollback; 1 = one snapshot published by the writer; other = 0 + error line). This contract pins that:
#   - the switch: member default 0, read with default 0, clamped to 0/1, documented = 0, echoed in byte order between .Failures and
#     .Seconds;
#   - the writer publishes only with the switch, under the store mutex, the pointer before the version, after every change of the
#     marked set (a new mark or an evicted mark in NoteObjectiveFailure, a credit that ended marks in NoteQuestCredit);
#   - the reader with the switch copies the pointer only through std::atomic_load and searches it with SnapshotMarked, after the
#     no-mark fast path and before the per-thread path, and takes neither the store mutex nor the store;
#   - the pointer is touched nowhere else (one declaration, one atomic store, two atomic loads);
#   - [StaleObjectiveMin] once per minute with the filter on, called from the world-thread report pass; the per-thread path counts
#     its rebuilds and lock waits;
#   - BuildSnapshot / SnapshotMarked are pure (no game header) and the unit test compares them with the store.
# Every check_* returns "" when it holds, else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "stale lockfree: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "stale lockfree: PB_MODULE_DIR is required")
endif()

set(TAG "stale lockfree")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("StaleObjectivePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_cpp)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
file(READ "${PB_MODULE_DIR}/t/quest_loop_policy_tests.cpp" unit_test)
string(REPLACE "\r" "" unit_test "${unit_test}")

# The text from first up to (not including) second; "" when either is missing.
function(region text first second out_var)
  set(${out_var} "" PARENT_SCOPE)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

# No ";" in a MATCHALL regex: each hit would split the result list in two.
function(count_regex text regex out)
  string(REGEX MATCHALL "${regex}" hits "${text}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# ---- the switch ----------------------------------------------------------------------------------------------------------
set(ECHO "            {\"QuestFirstProgression.StaleObjectiveFilter.Failures\", Value(questFirstProgressionStaleObjectiveFailures)},
            {\"QuestFirstProgression.StaleObjectiveFilter.LockFree\", Value(questFirstProgressionStaleObjectiveFilterLockFree)},
            {\"QuestFirstProgression.StaleObjectiveFilter.Seconds\", Value(questFirstProgressionStaleObjectiveSeconds)},")

function(check_switch config_h config_cpp conf_dist out)
  set(${out} "" PARENT_SCOPE)
  string(FIND "${config_h}" "uint32 questFirstProgressionStaleObjectiveFilterLockFree = 0;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: member default 0 missing" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "questFirstProgressionStaleObjectiveFilterLockFree = uint32(config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree\", 0));"
      "    if (questFirstProgressionStaleObjectiveFilterLockFree > 1)\n    {"
      "        questFirstProgressionStaleObjectiveFilterLockFree = 0;\n    }"
      "${ECHO}")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree = 0\n" at)
  if(at EQUAL -1)
    set(${out} "aiplayerbot.conf.dist.in: LockFree not documented = 0" PARENT_SCOPE)
  endif()
endfunction()

# ---- the writer ----------------------------------------------------------------------------------------------------------
function(check_writer travel_cpp out)
  set(${out} "" PARENT_SCOPE)
  region("${travel_cpp}" "    void PublishStaleObjectiveSnapshot(uint32 nowMs)" "\n    }\n" publish_fn)
  if(publish_fn STREQUAL "")
    set(${out} "TravelMgr.cpp: PublishStaleObjectiveSnapshot missing" PARENT_SCOPE)
    return()
  endif()
  # Switch 0: nothing built; a published snapshot is withdrawn (null pointer, then the version) so a reload 1 -> 0 -> 1 never
  # leaves the readers on a snapshot that missed the 0 phase (OB-10 review #958).
  string(FIND "${publish_fn}" "        if (!sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilterLockFree)\n        {\n            if (staleObjectiveSnapshotLive)\n            {\n                std::atomic_store_explicit(&staleObjectivePublished, std::shared_ptr<ai::stale_objective::Snapshot const>(), std::memory_order_release);\n                staleObjectivePublishVersion.fetch_add(1, std::memory_order_acq_rel);\n                staleObjectiveSnapshotLive = false;\n            }\n            return;\n        }\n        auto const start" gate)
  if(gate EQUAL -1)
    set(${out} "publish: not gated by the switch, or switch 0 does not withdraw a published snapshot (pointer before version)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${publish_fn}" "        std::atomic_store_explicit(&staleObjectivePublished, std::move(snapshot), std::memory_order_release);\n        staleObjectivePublishVersion.fetch_add(1, std::memory_order_acq_rel);\n        staleObjectiveSnapshotLive = true;\n" store_at)
  if(store_at EQUAL -1)
    set(${out} "publish: the pointer must be stored atomically before the version grows, then marked live" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${publish_fn}" "ai::stale_objective::BuildSnapshot(staleObjectiveStore, nowMs)" build_at)
  if(build_at EQUAL -1)
    set(${out} "publish: not built by BuildSnapshot" PARENT_SCOPE)
    return()
  endif()

  # A failure: published inside the lock scope, as its last statement, for a new or an evicted mark.
  region("${travel_cpp}" "void ai::stale_objective::NoteObjectiveFailure(" "\n}\n" note_fn)
  foreach(needle
      "marked = ai::stale_objective::RecordFailure(staleObjectiveStore, key, bot->GetGUIDLow(), nowMs, params, failures, bots, &evictedMark);"
      "        if (marked || evictedMark)\n            PublishStaleObjectiveSnapshot(nowMs);\n    }\n    if (marked)\n        sLog.outBasic(")
    string(FIND "${note_fn}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "NoteObjectiveFailure: missing (inside the lock): ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  # A credit that ended marks: published inside the lock scope.
  region("${travel_cpp}" "void ai::stale_objective::NoteQuestCredit(" "\n}\n" credit_fn)
  string(FIND "${credit_fn}" "        if (ended)\n        {\n            staleObjectiveStoreVersion.fetch_add(1, std::memory_order_acq_rel);\n            PublishStaleObjectiveSnapshot(WorldTimer::getMSTime());\n        }\n    }\n    if (ended)" at)
  if(at EQUAL -1)
    set(${out} "NoteQuestCredit: an ended mark is not published inside the lock" PARENT_SCOPE)
    return()
  endif()

  # The pointer: one declaration, two atomic stores (publish, withdraw), two atomic loads, nothing else.
  count_regex("${travel_cpp}" "staleObjectivePublished" all)
  count_regex("${travel_cpp}" "std::atomic_(store_explicit|load_explicit|load)\\(&staleObjectivePublished" atomic)
  if(NOT all EQUAL 5 OR NOT atomic EQUAL 4)
    set(${out} "TravelMgr.cpp: staleObjectivePublished used ${all} times, ${atomic} of them atomic (5 / 4 expected)" PARENT_SCOPE)
  endif()
endfunction()

# ---- the reader and the minute line --------------------------------------------------------------------------------------
function(check_reader travel_cpp travel_h mgr_cpp out)
  set(${out} "" PARENT_SCOPE)
  region("${travel_cpp}" "bool ai::stale_objective::IsObjectiveStale(" "\n}\n" query_fn)
  region("${query_fn}" "    if (sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilterLockFree)\n    {" "    // The thread's snapshot of the marked keys" lockfree)
  if(lockfree STREQUAL "")
    set(${out} "IsObjectiveStale: the LockFree branch is missing or not before the per-thread path" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${query_fn}" "if (!staleObjectiveAnyMark.load(std::memory_order_acquire) ||" fast_at)
  string(FIND "${query_fn}" "    if (sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilterLockFree)" branch_at)
  if(fast_at EQUAL -1 OR NOT fast_at LESS branch_at)
    set(${out} "IsObjectiveStale: the LockFree branch must come after the no-mark fast path" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "uint32 const current = staleObjectivePublishVersion.load(std::memory_order_acquire);"
      "published = std::atomic_load_explicit(&staleObjectivePublished, std::memory_order_acquire);"
      "return ai::stale_objective::SnapshotMarked(*published,")
    string(FIND "${lockfree}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "IsObjectiveStale LockFree branch: missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(banned "staleObjectiveMutex" "lock(" "staleObjectiveStore" "staleObjectiveStoreVersion")
    string(FIND "${lockfree}" "${banned}" at)
    if(NOT at EQUAL -1)
      set(${out} "IsObjectiveStale LockFree branch: must not use ${banned}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "staleObjectiveReaderLockWaits.fetch_add(1, std::memory_order_relaxed);"
      "staleObjectiveRebuilds.fetch_add(1, std::memory_order_relaxed);")
    string(FIND "${query_fn}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "IsObjectiveStale per-thread path: counter missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  region("${travel_cpp}" "void ai::stale_objective::ReportMinute()" "\n}\n" minute_fn)
  foreach(needle
      "if (!sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilter || now < lastReport + 60)\n        return;"
      "[StaleObjectiveMin] lockfree=%u rebuilds=%llu reader_lock_waits=%llu reader_wait_us=%llu publishes=%llu \"\n        \"publish_us=%llu published_keys=%u")
    string(FIND "${minute_fn}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "ReportMinute: missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${travel_h}" "void ReportMinute();" at)
  count_regex("${mgr_cpp}" "ai::stale_objective::ReportMinute\\(\\)" calls)
  if(at EQUAL -1 OR NOT calls EQUAL 1)
    set(${out} "ReportMinute: declared and called once from RandomPlayerbotMgr (calls: ${calls})" PARENT_SCOPE)
  endif()
endfunction()

# ---- the pure part -------------------------------------------------------------------------------------------------------
function(check_pure policy unit_test out)
  set(${out} "" PARENT_SCOPE)
  foreach(needle "inline Snapshot BuildSnapshot(Store const& store, std::uint32_t nowMs)"
      "inline bool SnapshotMarked(Snapshot const& snapshot, Key const& key, std::uint32_t nowMs)"
      "return it != snapshot.end() && it->key == key && std::int32_t(it->untilMs - nowMs) > 0;"
      "    if (evictedMark && IsMarked(oldest->second, nowMs))\n        *evictedMark = true;\n    store.entries.erase(oldest);")
    string(FIND "${policy}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "StaleObjectivePolicy.h: missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${policy}" "#include \"" at)
  if(NOT at EQUAL -1)
    set(${out} "StaleObjectivePolicy.h: a game header in the pure policy" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "Require(so::SnapshotMarked(snap, k, now) == so::IsStale(s, k, now), \"snapshot and store give the same answer\");"
      "\"full of marks: the oldest mark is evicted and reported\"")
    string(FIND "${unit_test}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "quest_loop_policy_tests.cpp: missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# ---- run -----------------------------------------------------------------------------------------------------------------
set(failures "")
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_writer("${travel_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_reader("${travel_cpp}" "${travel_h}" "${mgr_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_pure("${policy}" "${unit_test}" r)
if(r)
  list(APPEND failures "${r}")
endif()
if(failures)
  string(REPLACE ";" "\n  " text "${failures}")
  message(FATAL_ERROR "${TAG}:\n  ${text}")
endif()

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------
set(probes 0)
function(expect_caught reason what)
  if(reason STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
  math(EXPR n "${probes} + 1")
  set(probes ${n} PARENT_SCOPE)
endfunction()

string(REPLACE "uint32 questFirstProgressionStaleObjectiveFilterLockFree = 0;" "uint32 questFirstProgressionStaleObjectiveFilterLockFree = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree\", 0)" "\"AiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "    if (questFirstProgressionStaleObjectiveFilterLockFree > 1)\n" "    if (false)\n" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no clamp of an unknown value")

string(REPLACE "            {\"QuestFirstProgression.StaleObjectiveFilter.LockFree\", Value(questFirstProgressionStaleObjectiveFilterLockFree)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "not in [ConfigEcho]")

string(REPLACE "\nAiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree = 0\n" "\nAiPlayerbot.QuestFirstProgression.StaleObjectiveFilter.LockFree = 1\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "documented 1")

string(REPLACE "        if (!sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilterLockFree)\n        {\n" "        if (false)\n        {\n" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "publish without the switch")

string(REPLACE "            if (staleObjectiveSnapshotLive)\n" "            if (false)\n" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "switch 0 never withdraws the snapshot (reload 1 -> 0 -> 1)")

string(REPLACE "                std::atomic_store_explicit(&staleObjectivePublished, std::shared_ptr<ai::stale_objective::Snapshot const>(), std::memory_order_release);\n                staleObjectivePublishVersion.fetch_add(1, std::memory_order_acq_rel);\n"
  "                staleObjectivePublishVersion.fetch_add(1, std::memory_order_acq_rel);\n                std::atomic_store_explicit(&staleObjectivePublished, std::shared_ptr<ai::stale_objective::Snapshot const>(), std::memory_order_release);\n" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "the withdraw bumps the version before the null pointer")

string(REPLACE "        staleObjectiveSnapshotLive = true;\n" "" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "a publish not marked live (never withdrawn)")

string(REPLACE "        std::atomic_store_explicit(&staleObjectivePublished, std::move(snapshot), std::memory_order_release);\n        staleObjectivePublishVersion.fetch_add(1, std::memory_order_acq_rel);\n"
  "        staleObjectivePublishVersion.fetch_add(1, std::memory_order_acq_rel);\n        std::atomic_store_explicit(&staleObjectivePublished, std::move(snapshot), std::memory_order_release);\n" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "the version before the pointer")

string(REPLACE "        if (marked || evictedMark)\n            PublishStaleObjectiveSnapshot(nowMs);\n" "" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "a new mark is not published")

string(REPLACE "        if (marked || evictedMark)\n" "        if (marked)\n" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "an evicted mark is not published")

string(REPLACE "        if (marked || evictedMark)\n            PublishStaleObjectiveSnapshot(nowMs);\n    }\n    if (marked)\n"
  "    }\n    if (marked || evictedMark)\n        PublishStaleObjectiveSnapshot(nowMs);\n    if (marked)\n" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "published after the lock")

string(REPLACE "            PublishStaleObjectiveSnapshot(WorldTimer::getMSTime());\n" "" m "${travel_cpp}")
check_writer("${m}" r)
expect_caught("${r}" "a credit is not published")

check_writer("${travel_cpp}\nauto const raw = staleObjectivePublished.get();" r)
expect_caught("${r}" "a non-atomic use of the pointer")

string(REPLACE "        if (current != publishedVersion)\n        {\n" "        if (current != publishedVersion)\n        {\n            std::lock_guard<std::mutex> lock(staleObjectiveMutex);\n" m "${travel_cpp}")
check_reader("${m}" "${travel_h}" "${mgr_cpp}" r)
expect_caught("${r}" "the LockFree reader takes the store mutex")

string(REPLACE "            return ai::stale_objective::SnapshotMarked(*published,\n" "            return ai::stale_objective::IsStale(staleObjectiveStore,\n" m "${travel_cpp}")
check_reader("${m}" "${travel_h}" "${mgr_cpp}" r)
expect_caught("${r}" "the LockFree reader reads the store")

string(REPLACE "    if (sPlayerbotAIConfig.questFirstProgressionStaleObjectiveFilterLockFree)\n    {" "    if (false)\n    {" m "${travel_cpp}")
check_reader("${m}" "${travel_h}" "${mgr_cpp}" r)
expect_caught("${r}" "the LockFree branch never taken")

string(REPLACE "                staleObjectiveReaderLockWaits.fetch_add(1, std::memory_order_relaxed);\n" "" m "${travel_cpp}")
check_reader("${m}" "${travel_h}" "${mgr_cpp}" r)
expect_caught("${r}" "lock waits not counted")

string(REPLACE "    ai::stale_objective::ReportMinute();" "    // gone" m "${mgr_cpp}")
check_reader("${travel_cpp}" "${travel_h}" "${m}" r)
expect_caught("${r}" "the minute line never called")

string(REPLACE "publish_us=%llu published_keys=%u" "published_keys=%u" m "${travel_cpp}")
check_reader("${m}" "${travel_h}" "${mgr_cpp}" r)
expect_caught("${r}" "a counter missing in [StaleObjectiveMin]")

check_pure("${policy}\n#include \"playerbot/playerbot.h\"" "${unit_test}" r)
expect_caught("${r}" "game include in the policy")

string(REPLACE "    if (evictedMark && IsMarked(oldest->second, nowMs))\n        *evictedMark = true;\n" "" m "${policy}")
check_pure("${m}" "${unit_test}" r)
expect_caught("${r}" "an evicted mark not reported")

string(REPLACE "Require(so::SnapshotMarked(snap, k, now) == so::IsStale(s, k, now), \"snapshot and store give the same answer\");" "" m "${unit_test}")
check_pure("${policy}" "${m}" r)
expect_caught("${r}" "the unit test does not compare snapshot and store")

message(STATUS "${TAG}: OK (${probes} negative probes caught)")
