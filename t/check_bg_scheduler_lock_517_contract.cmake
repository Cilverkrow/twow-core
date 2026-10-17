# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)

if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#517 R0 (OB-00 D1 16.10.2026, core bugfix without a switch): BattleGroundMgr's queue update
# scheduler is written from map threads (BattleGround update and leave, invite expiry, level-up) and
# swapped on the world thread. Both sides hold SchedulerLock, a short std::mutex: one find/emplace or
# one swap. The queue updates themselves run on the local copy without the lock (no nesting with the
# queue's own m_Lock, and a queue update that schedules again cannot deadlock).

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#517: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#517: ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#517: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Body of one function: from its signature to the first closing brace at column 0.
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "#517: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if (end EQUAL -1)
    message(FATAL_ERROR "#517: end of function not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while (TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if (offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("src/game/Battlegrounds/BattleGroundMgr.h" mgr_h)
read_source("src/game/Battlegrounds/BattleGroundMgr.cpp" mgr_cpp)

require_text("${mgr_h}" "        std::mutex SchedulerLock;" "scheduler lock (plain std::mutex)")
forbid_text("${mgr_h}" "shared_mutex SchedulerLock" "SchedulerLock must stay a plain std::mutex")

# Writer: the find and the emplace both under the lock.
function_body("${mgr_cpp}" "void BattleGroundMgr::ScheduleQueueUpdate(" schedule_body)
require_order("${schedule_body}" "std::lock_guard<std::mutex> guard(SchedulerLock);" "std::find(m_QueueUpdateScheduler.begin()"
  "lock before the duplicate check")
require_order("${schedule_body}" "std::lock_guard<std::mutex> guard(SchedulerLock);" "m_QueueUpdateScheduler.emplace_back(schedule_id);"
  "lock before the emplace")

# Reader: only the swap under the lock, the queue updates after the scope closed.
function_body("${mgr_cpp}" "void BattleGroundMgr::Update(uint32 diff)" update_body)
require_text("${update_body}"
  "    std::vector<uint64> scheduled;\n    {\n        std::lock_guard<std::mutex> guard(SchedulerLock);\n        std::swap(scheduled, m_QueueUpdateScheduler);\n    }\n"
  "swap in its own lock scope")
require_order("${update_body}" "std::swap(scheduled, m_QueueUpdateScheduler);\n    }\n" "m_BattleGroundQueues[bgQueueTypeId].Update(bgTypeId, bracket_id);"
  "queue updates outside the lock scope")
forbid_text("${update_body}" "if (!m_QueueUpdateScheduler.empty())" "unlocked emptiness check on the shared vector")

# Every other touch of the vector sits in these two functions.
count_text("${mgr_cpp}" "m_QueueUpdateScheduler" total_uses)
count_text("${schedule_body}" "m_QueueUpdateScheduler" schedule_uses)
count_text("${update_body}" "m_QueueUpdateScheduler" update_uses)
math(EXPR guarded "${schedule_uses} + ${update_uses}")
if (NOT total_uses EQUAL guarded)
  message(FATAL_ERROR "#517: m_QueueUpdateScheduler used outside ScheduleQueueUpdate/Update (total ${total_uses}, guarded ${guarded})")
endif()
# Update touches the vector exactly once (the swap), ScheduleQueueUpdate four times (find begin/end, end, emplace).
if (NOT update_uses EQUAL 1 OR NOT schedule_uses EQUAL 4)
  message(FATAL_ERROR "#517: unexpected m_QueueUpdateScheduler uses (Update ${update_uses}, ScheduleQueueUpdate ${schedule_uses}); every use must stay under SchedulerLock")
endif()

message(STATUS "#517 R0 BattleGroundMgr scheduler lock contract: OK (${total_uses} guarded uses)")
