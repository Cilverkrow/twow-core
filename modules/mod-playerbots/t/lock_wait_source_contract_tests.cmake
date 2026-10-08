# twow-repo#541 (deep dive C1 + [LockWait], OB-00 go 08.10.2026; no behaviour change).
# C1: GetAreaLevel reads the area levels frozen at the end of LoadAreaLevels without the global lock.
# [LockWait]: behind AiPlayerbot.LockWaitTrace (default off) - waits on the area-level and bot-log locks,
# travel searches started, the core DB counters; one line per minute from UpdateAIInternal.
# Pure table logic: t/area_level_policy_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("TravelMgr.cpp" travel_mgr)
read_source("TravelMgr.h" travel_h)
read_source("LockWaitTrace.h" trace_h)
read_source("BotLog.cpp" bot_log)
read_source("strategy/values/TravelValues.h" travel_values)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# --- C1: lock-free read path -------------------------------------------------------------------------
require_text("${travel_h}" "std::atomic<bool> areaLevelsFrozen{ false };" "freeze flag")

string(FIND "${travel_mgr}" "int32 TravelMgr::GetAreaLevel(uint32 area_id)" get_at)
string(FIND "${travel_mgr}" "void TravelMgr::LoadCreatureAreaLevels()" load_at)
if(get_at EQUAL -1 OR load_at LESS get_at)
  message(FATAL_ERROR "#541: GetAreaLevel / LoadCreatureAreaLevels not found")
endif()
math(EXPR get_len "${load_at} - ${get_at}")
string(SUBSTRING "${travel_mgr}" ${get_at} ${get_len} get_body)
# Flag (acquire), overrides first, then the frozen table - all before the lock.
require_order("${get_body}" "if (areaLevelsFrozen.load(std::memory_order_acquire))" "sPlayerbotAIConfig.areaLevelOverrides.find(area_id)" "flag before overrides")
require_order("${get_body}" "sPlayerbotAIConfig.areaLevelOverrides.find(area_id)" "ai::area_level::FrozenLookup(frozenAreaLevels, area_id, frozenLevel)" "overrides before the table")
require_order("${get_body}" "ai::area_level::FrozenLookup(frozenAreaLevels, area_id, frozenLevel)" "ai::lock_wait::TimedLock<std::recursive_mutex> lock(areaLevelMutex, ai::lock_wait::AreaLevel);" "table before the lock")
# The fast path never writes.
string(FIND "${get_body}" "ai::lock_wait::TimedLock<std::recursive_mutex> lock(areaLevelMutex" lock_at)
string(SUBSTRING "${get_body}" 0 ${lock_at} fast_path)
string(FIND "${fast_path}" "areaLevels[" fast_write)
string(FIND "${fast_path}" "frozenAreaLevels =" fast_freeze)
if(NOT fast_write EQUAL -1 OR NOT fast_freeze EQUAL -1)
  message(FATAL_ERROR "#541: the lock-free path of GetAreaLevel must not write")
endif()
string(FIND "${get_body}" "std::lock_guard<std::recursive_mutex> lock(areaLevelMutex);" plain_lock)
if(NOT plain_lock EQUAL -1)
  message(FATAL_ERROR "#541: GetAreaLevel takes the area-level lock only through TimedLock")
endif()

# Frozen once, after the generation loop, under the lock; the early return of a reload freezes too.
string(FIND "${travel_mgr}" "void TravelMgr::LoadAreaLevels()" area_load_at)
string(FIND "${travel_mgr}" "void TravelMgr::SetMobAvoidArea()" avoid_at)
if(area_load_at EQUAL -1 OR avoid_at LESS area_load_at)
  message(FATAL_ERROR "#541: LoadAreaLevels not found")
endif()
math(EXPR area_load_len "${avoid_at} - ${area_load_at}")
string(SUBSTRING "${travel_mgr}" ${area_load_at} ${area_load_len} area_load_body)
require_order("${area_load_body}" "std::lock_guard<std::recursive_mutex> lock(areaLevelMutex);" "if (!areaLevels.empty())" "freeze under the lock")
require_order("${area_load_body}" "if (!areaLevels.empty())" "if (!areaLevelsFrozen.load(std::memory_order_acquire))" "reload freezes once")
require_order("${area_load_body}" "frozenAreaLevels = ai::area_level::FreezeLevels(levels" "areaLevelsFrozen.store(true, std::memory_order_release);" "table before the flag (reload)")
string(FIND "${area_load_body}" ">> Generated " generated_at)
if(generated_at EQUAL -1)
  message(FATAL_ERROR "#541: generation log line not found in LoadAreaLevels")
endif()
string(SUBSTRING "${area_load_body}" ${generated_at} -1 after_generation)
require_order("${after_generation}" "frozenAreaLevels = ai::area_level::FreezeLevels(levels" "areaLevelsFrozen.store(true, std::memory_order_release);" "freeze after the generation, table before the flag")

# --- [LockWait] ----------------------------------------------------------------------------------------
require_text("${trace_h}" "static std::atomic<bool> enabled{false};" "switch default off")
require_order("${trace_h}" "if (!Enabled().load(std::memory_order_relaxed))" "if (mutex.try_lock())" "switch before try_lock")
require_order("${trace_h}" "if (mutex.try_lock())" "Get(site).Add(" "fast path before timing")

string(FIND "${bot_log}" "std::lock_guard<std::mutex> _g(m_mutex);" botlog_plain)
if(NOT botlog_plain EQUAL -1)
  message(FATAL_ERROR "#541: BOTLOG_IMPL must lock through TimedLock")
endif()
require_text("${bot_log}" "ai::lock_wait::TimedLock<std::mutex> _g(m_mutex, ai::lock_wait::BotLogMutex);" "bot log lock trace")
# L1 stays: level check before format and lock.
require_text("${bot_log}" "#define BOTLOG_SKIP_BELOW(level)" "L1 skip")

require_text("${travel_values}" "operator=(std::future<PartitionedTravelList>&& job) { if (job.valid()) ai::lock_wait::AsyncStarts().fetch_add(1" "travel search counter")

require_text("${config_cpp}" "config.GetBoolDefault(\"AiPlayerbot.LockWaitTrace\", false)" "config switch default off")
require_text("${config_cpp}" "ai::lock_wait::Enabled().store(lockWaitTrace" "module switch")
require_text("${config_cpp}" "Database::LockWaitTrace().store(lockWaitTrace" "core DB switch")
require_text("${conf_dist}" "#AiPlayerbot.LockWaitTrace = 0" "documented key")

require_text("${rnd_mgr}" "\"[LockWait] area_level=" "minute line")
require_text("${rnd_mgr}" "CharacterDatabase.TakeLockWaits(Database::LOCK_WAIT_CONNECTION)" "core DB counters")
string(FIND "${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd_at)
if(upd_at EQUAL -1)
  message(FATAL_ERROR "#541: UpdateAIInternal not found")
endif()
string(SUBSTRING "${rnd_mgr}" ${upd_at} 1200 upd_head)
require_order("${upd_head}" "\n    ProcessParkedBots();" "\n    ReportLockWaits();" "minute line in the world-thread pass")

message(STATUS "lock_wait source contract passed")
