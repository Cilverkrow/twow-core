if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#541 (deep dive C3, OB-00 go 08.10.2026): DB lock-wait counters for the playerbot [LockWait]
# line. Off by default (Database::LockWaitTrace, set from AiPlayerbot.LockWaitTrace). With the switch off
# SqlConnection::Lock is a plain lock and AddToDelayQueue a plain add; with it on only a contended lock
# and an enqueue >= 10 us are timed. Pure measurement: no behaviour change.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#541: missing ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("src/shared/Database/Database.h" db_h)
read_source("src/shared/Database/Database.cpp" db_cpp)

# The lock is no longer taken in a member initializer (it is timed in the out-of-line constructor).
string(FIND "${db_h}" "m_lock{m_pConn->m_mutex}" inline_lock)
if (NOT inline_lock EQUAL -1)
  message(FATAL_ERROR "#541: SqlConnection::Lock must lock in its constructor (Database.cpp)")
endif()
require_text("${db_h}" "Lock(SqlConnection * conn);" "out-of-line Lock constructor")
require_text("${db_h}" "void AddToDelayQueue(SqlOperation* op);" "out-of-line AddToDelayQueue")
require_text("${db_h}" "static std::atomic<bool>& LockWaitTrace();" "switch")
require_text("${db_h}" "LockWaitStats TakeLockWaits(LockWaitSite site);" "reader")

# Default off.
require_text("${db_cpp}" "static std::atomic<bool> enabled{false};" "switch default off")

# Lock: off -> plain lock; on -> try_lock fast path, timed only when contended.
string(FIND "${db_cpp}" "SqlConnection::Lock::Lock(SqlConnection* conn)" lock_at)
string(FIND "${db_cpp}" "void Database::AddToDelayQueue(SqlOperation* op)" enq_at)
if (lock_at EQUAL -1 OR enq_at EQUAL -1 OR NOT lock_at LESS enq_at)
  message(FATAL_ERROR "#541: Lock constructor / AddToDelayQueue not found in the expected order")
endif()
math(EXPR lock_len "${enq_at} - ${lock_at}")
string(SUBSTRING "${db_cpp}" ${lock_at} ${lock_len} lock_body)
require_text("${lock_body}" "m_lock(conn->m_mutex, std::defer_lock)" "deferred lock")
require_order("${lock_body}" "if (!Database::LockWaitTrace().load(std::memory_order_relaxed))" "if (m_lock.try_lock())" "switch before try_lock")
require_order("${lock_body}" "if (m_lock.try_lock())" "RecordLockWait(Database::LOCK_WAIT_CONNECTION" "fast path before timing")

string(SUBSTRING "${db_cpp}" ${enq_at} 600 enq_body)
require_text("${enq_body}" "m_delayQueue->add(op);" "enqueue still adds")
require_text("${enq_body}" "RecordLockWait(LOCK_WAIT_ENQUEUE, us);" "enqueue timing")

message(STATUS "db_lock_wait_541 contract passed")
