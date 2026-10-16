if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#597 (TICK-01, Refs #592 #541): AsyncQueries.UnsafeBudgetMs, the time budget of the thread-unsafe
# async query callbacks (logins) that SqlResultQueue::Update runs on the world thread. This contract pins the
# default 0 (documented), the hand-over from the world config, the check BEFORE the next callback is taken off
# the queue, the progress guarantee (the first callback of a pass always runs), the kept order (no re-queue),
# the unchanged path at 0 (no clock read, the old timeout check as before), the thread-safe callbacks left
# alone and the [UnsafeBudget] minute line. Pure logic: t/unsafe_budget_597_test.cpp.

function(read_source path out_var)
  if (NOT EXISTS "${TW_CORE_ROOT}/${path}")
    message(FATAL_ERROR "#597: source missing: ${path}")
  endif()
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#597: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#597: forbidden ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#597: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Number of occurrences of needle in text.
function(count_text text needle out_var)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  set(rest "${text}")
  while (TRUE)
    string(FIND "${rest}" "${needle}" at)
    if (at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${at} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# The text between first (inclusive) and second (exclusive).
function(slice text first second out_var)
  string(FIND "${text}" "${first}" a)
  if (a EQUAL -1)
    message(FATAL_ERROR "#597: slice start not found: ${first}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${second}" b)
  if (b EQUAL -1)
    message(FATAL_ERROR "#597: slice end not found: ${second}")
  endif()
  string(SUBSTRING "${tail}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("src/game/World.h" world_h)
read_source("src/game/World.cpp" world_cpp)
read_source("src/shared/Database/SqlOperations.h" sql_h)
read_source("src/shared/Database/SqlOperations.cpp" sql_cpp)
read_source("src/shared/Database/UnsafeBudgetPolicy.h" policy_h)
read_source("src/mangosd/mangosd.conf.dist.in" dist)
read_source("CMakeLists.txt" root_cmake)

# ---- Switch: default 0, documented with that default, handed to SqlOperations (also on a reload).
require_text("${world_h}" "CONFIG_UINT32_ASYNC_QUERIES_UNSAFE_BUDGET_MS," "config index")
require_text("${world_cpp}" "setConfig(CONFIG_UINT32_ASYNC_QUERIES_UNSAFE_BUDGET_MS, \"AsyncQueries.UnsafeBudgetMs\", 0);" "UnsafeBudgetMs default 0")
require_order("${world_cpp}" "setConfig(CONFIG_UINT32_ASYNC_QUERIES_UNSAFE_BUDGET_MS, \"AsyncQueries.UnsafeBudgetMs\", 0);"
  "SqlResultQueue::SetUnsafeBudgetMs(getConfig(CONFIG_UINT32_ASYNC_QUERIES_UNSAFE_BUDGET_MS));" "value read before it is handed over")
require_text("${dist}" "\nAsyncQueries.UnsafeBudgetMs = 0\n" "documented default 0")
slice("${dist}" "# AsyncQueries.UnsafeBudgetMs" "\nAsyncQueries.UnsafeBudgetMs = 0\n" dist_doc)
require_text("${dist_doc}" "Default: 0 (off)" "documented as off by default")
require_text("${dist_doc}" "\"[UnsafeBudget] calls_capped=<n> deferred_max=<n> ran=<n> loop_max_ms=<x>\"" "line documented")
# Review follow-up: the pre-existing PlayerbotMgr lifetime hazard that N > 0 widens stays documented.
require_text("${dist_doc}" "Caution (mod-playerbots): a '.bot add' bot login" "bot-login lifetime caution documented")
require_text("${dist}" "\nAsyncQueriesTickTimeout = 15000\n" "AsyncQueriesTickTimeout untouched")
require_text("${sql_h}" "static void SetUnsafeBudgetMs(uint32 ms);" "setter")
require_text("${sql_h}" "static std::atomic<uint32> s_unsafeBudgetMs;" "relaxed atomic budget")
require_text("${sql_cpp}" "std::atomic<uint32> SqlResultQueue::s_unsafeBudgetMs{0};" "budget off until set")
require_text("${sql_cpp}" "s_unsafeBudgetMs.store(ms, std::memory_order_relaxed);" "relaxed store")

# ---- SqlResultQueue::Update: budget latched once per pass, only the unsafe loop.
slice("${sql_cpp}" "void SqlResultQueue::Update(uint32 timeout)" "#ifndef DO_POSTGRESQL" update_fn)
count_text("${sql_cpp}" "s_unsafeBudgetMs.load(" budget_loads)
if (NOT budget_loads EQUAL 1)
  message(FATAL_ERROR "#597: the budget must be latched exactly once per pass (found ${budget_loads} loads)")
endif()
require_text("${update_fn}" "uint32 const budgetMs = s_unsafeBudgetMs.load(std::memory_order_relaxed);" "budget latched in Update")
# Thread-safe callbacks (callback pool) are not budgeted.
slice("${update_fn}" "while (next(callback))" "std::future<void> job = m_callbackThreads->processWorkload();" safe_loop)
forbid_text("${safe_loop}" "budgetMs" "budget in the thread-safe dispatch")
forbid_text("${safe_loop}" "unsafe_budget::" "budget in the thread-safe dispatch")
slice("${update_fn}" "std::future<void> job = m_callbackThreads->processWorkload();" "if (job.valid())" unsafe_loop)
require_order("${unsafe_loop}" "uint32 const budgetMs = s_unsafeBudgetMs.load(std::memory_order_relaxed);" "for (;;)" "latched before the unsafe loop")

# The check BEFORE the next callback is taken off the queue (so a stop leaves it at the front) ...
require_order("${unsafe_loop}" "unsafe_budget::StopBefore(budgetMs, unsafeRan, tick_diag::NowUs() - unsafeStartUs)"
  "if (!_threadUnsafeWaitingQueries.next(s))" "budget check before the callback is taken")
require_order("${unsafe_loop}" "if (!_threadUnsafeWaitingQueries.next(s))" "s->Execute();" "callback taken, then run")
count_text("${unsafe_loop}" "_threadUnsafeWaitingQueries.next(" unsafe_nexts)
if (NOT unsafe_nexts EQUAL 1)
  message(FATAL_ERROR "#597: the unsafe loop takes callbacks in exactly one place (found ${unsafe_nexts})")
endif()
# ... with the progress guarantee (nothing stops before one callback ran) and 0 = no check at all.
require_text("${unsafe_loop}" "if (budgetMs && unsafeRan && numUnsafeQueries &&\n            unsafe_budget::StopBefore(" "guard: budget on, one ran, one left")
require_text("${unsafe_loop}" "{\n            budgetCapped = true;\n            break;\n        }" "a stop only ends the pass")
require_text("${policy_h}" "return budgetMs && ranThisPass && elapsedUs >= std::uint64_t(budgetMs) * 1000;" "0 = never, first always runs, stop at >= budget")
require_order("${unsafe_loop}" "delete s;\n        --numUnsafeQueries;\n        ++unsafeRan;" "if (timeout && WorldTimer::getMSTimeDiffToNow(begin) > timeout)" "run counted per callback, before the timeout check")
# Order kept: nothing is put back into the unsafe queue or skipped.
forbid_text("${unsafe_loop}" "_threadUnsafeWaitingQueries.add" "re-queue in the unsafe loop")
forbid_text("${unsafe_loop}" "continue;" "skipping a callback in the unsafe loop")
# The existing timeout check stays as it was, after the callback.
require_text("${unsafe_loop}" "        if (timeout && WorldTimer::getMSTimeDiffToNow(begin) > timeout)\n            break;\n    }" "timeout check unchanged")
require_text("${update_fn}" "uint32 begin = WorldTimer::getMSTime();" "timeout start unchanged")
require_text("${update_fn}" "if (numUnsafeQueries > 1000) // Bottleneck here" "remaining warning unchanged")

# Unchanged path at 0: every clock read in the unsafe loop sits behind the budget or [ResultsSlow].
require_text("${unsafe_loop}" "if (budgetMs && !unsafeRan)\n            unsafeStartUs = tick_diag::NowUs();" "start clock only with the budget")
require_text("${unsafe_loop}" "if (budgetMs && unsafeRan)  // twow-repo#597: [UnsafeBudget] counters, only with the budget\n        s_unsafeBudgetCounters.Record(unsafeRan, budgetCapped, numUnsafeQueries, tick_diag::NowUs() - unsafeStartUs);" "counters only with the budget")
require_text("${unsafe_loop}" "uint64 const startUs = slowMs ? tick_diag::NowUs() : 0;" "[ResultsSlow] clock unchanged")
require_text("${unsafe_loop}" "if (slowMs)\n            ReportSlowCallback(tick_diag::NowUs() - startUs, true, typeName, numUnsafeQueries, slowMs);" "[ResultsSlow] report unchanged")
count_text("${unsafe_loop}" "tick_diag::NowUs()" unsafe_clocks)
if (NOT unsafe_clocks EQUAL 5)
  message(FATAL_ERROR "#597: the unsafe loop has 5 guarded clock reads (budget check, budget start, [ResultsSlow] start/end, counters); found ${unsafe_clocks}")
endif()

# ---- [UnsafeBudget]: fixed counters, one line a minute from the world thread, only with the budget.
require_text("${policy_h}" "\"[UnsafeBudget] calls_capped=%llu deferred_max=%llu ran=%llu loop_max_ms=%.1f\"" "[UnsafeBudget] line")
require_text("${sql_cpp}" "unsafe_budget::Counters s_unsafeBudgetCounters;" "one fixed set of counters")
slice("${sql_cpp}" "void SqlResultQueue::ReportUnsafeBudget(bool log)" "void SqlResultQueue::Update(uint32 timeout)" report_fn)
require_order("${report_fn}" "s_unsafeBudgetCounters.Take();" "if (!log)" "counters taken (reset) even without a line")
require_text("${report_fn}" "sLog.out(LOG_PERFORMANCE, \"%s\", line);" "line into the performance log")
slice("${world_cpp}" "void World::UpdateResultQueue()" "void World::UnsafeBudgetMinute()" urq_fn)
require_order("${urq_fn}" "LoginDatabase.ProcessResultQueue(getConfig(CONFIG_UINT32_ASYNC_QUERIES_TICK_TIMEOUT));" "UnsafeBudgetMinute();" "minute line after the three queues")
slice("${world_cpp}" "void World::UnsafeBudgetMinute()" "void World::UpdateRealmCharCount(uint32 accountId)" minute_fn)
require_order("${minute_fn}" "if (!getConfig(CONFIG_UINT32_ASYNC_QUERIES_UNSAFE_BUDGET_MS))" "std::chrono::steady_clock::now()" "no clock read when off")
require_text("${minute_fn}" "now - m_unsafeBudgetMinuteStart < std::chrono::seconds(60)" "once a minute")
require_order("${minute_fn}" "SqlResultQueue::ReportUnsafeBudget(false);" "SqlResultQueue::ReportUnsafeBudget(true);" "switch-on starts with fresh counters")
require_text("${world_h}" "std::chrono::steady_clock::time_point m_unsafeBudgetMinuteStart{};" "window starts closed")
# Bounded: no container in the policy, only short std::mutex (none needed here), never shared_mutex.
foreach (container "<vector>" "<map>" "<deque>" "<list>" "<unordered_map>" "<string>")
  forbid_text("${policy_h}" "#include ${container}" "growing container in the policy")
endforeach()
forbid_text("${policy_h}" "shared_mutex" "shared_mutex in the policy")
forbid_text("${sql_cpp}" "shared_mutex" "shared_mutex in SqlOperations")

# ---- Tests registered like the other #597 suites; tagged.
require_text("${root_cmake}" "add_executable(unsafe_budget_597_test\n    \"\${TW_CORE_ROOT}/t/unsafe_budget_597_test.cpp\")" "unit test target")
require_text("${root_cmake}" "add_test(NAME unsafe_budget_597_test COMMAND unsafe_budget_597_test)" "unit test registered")
require_text("${root_cmake}" "-P \"\${TW_CORE_ROOT}/t/check_unsafe_budget_597_contract.cmake\")" "contract registered")
foreach (src world_h world_cpp sql_h sql_cpp policy_h dist)
  require_text("${${src}}" "twow-repo#597" "twow-repo#597 tag in ${src}")
endforeach()

message(STATUS "UNSAFE_BUDGET_597_CONTRACT=PASS")
