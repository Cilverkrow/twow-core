if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#541 (owner approval 07.10.2026): where does the server wait? [WorldTick] phases of the
# world period, [MapTick] region durations and barrier spread, thread names for /proc.
# Pure aggregation: t/world_tick_trace_test.cpp. This contract locks the default-off switch, the
# timing hooks and the thread names. Pure measurement: no behaviour change.

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

read_source("src/game/World.h" world_h)
read_source("src/game/World.cpp" world_cpp)
read_source("src/game/Maps/MapManager.cpp" mapmgr)
read_source("src/game/Maps/Map.cpp" map_cpp)
read_source("src/shared/ThreadPool.cpp" pool)
read_source("src/mangosd/WorldRunnable.cpp" runnable)
read_source("src/mangosd/mangosd.conf.dist.in" dist)

# Switch, default off, latched once per tick; every hook asks the latched flag.
require_text("${world_cpp}" "setConfig(CONFIG_BOOL_PERFLOG_WORLD_TICK, \"PerformanceLog.WorldTick\", false);" "default-off switch")
require_text("${dist}" "PerformanceLog.WorldTick = 0" "documented off")
require_text("${world_cpp}" "m_worldTickOn = getConfig(CONFIG_BOOL_PERFLOG_WORLD_TICK);" "flag latched per tick")
require_text("${world_h}" "void WorldTickAdd(world_tick::Phase phase, uint64 us) { if (m_worldTickOn) m_worldTick.Add(phase, us); }" "phase add only when on")
require_text("${world_h}" "void MapTickAdd(std::vector<std::pair<uint64, uint32>> const& regions) { if (m_worldTickOn) m_mapTick.AddTick(regions); }" "region add only when on")

# World phases around the real calls.
require_order("${world_cpp}" "uint64 const sessionsStartUs" "UpdateSessions(diff);" "sessions timed")
require_order("${world_cpp}" "uint64 const mapMgrStartUs" "sMapMgr.Update(diff);" "map manager excluded from managers")
require_order("${world_cpp}" "uint64 const resultsStartUs" "UpdateResultQueue();" "results timed")
require_text("${world_cpp}" "WorldTickAdd(world_tick::AsyncTasks" "async task wait timed")

# Map manager phases: barrier and instances while waiting.
require_order("${mapmgr}" "endPhase(world_tick::MapsPre);" "do {" "maps_pre before the wait loop")
require_order("${mapmgr}" "if (continents.valid())\n        continents.wait();" "sWorld.WorldTickAdd(world_tick::WaitContinents" "barrier measured after continents.wait()")
require_text("${mapmgr}" "m->SetRegionUpdateUs(uint32(World::WorldTickNowUs() - regionStartUs));" "region duration written by its thread")
require_text("${mapmgr}" "sWorld.MapTickAdd(regions);" "regions handed to [MapTick]")
require_text("${mapmgr}" "snprintf(name, sizeof(name), \"Reg%u.%u\", m->GetId(), m->GetInstanceId());" "region thread name")

# Lines once per minute, never per tick.
require_text("${world_cpp}" "if (windowMs < 60 * IN_MILLISECONDS)\n        return;" "one block per minute")
require_text("${world_cpp}" "\"[WorldTick] interval=\"" "WorldTick line")
require_text("${world_cpp}" "[MapTick] ticks=%u regions=%u waste_ms=" "MapTick barrier line")
require_text("${runnable}" "sWorld.WorldTickEnd(updateEndUs - updateStartUs, World::WorldTickNowUs() - updateEndUs);" "tick closed after the sleep")

# Thread names fit Linux's 15 characters; pools carry their region.
require_text("${pool}" "Name.substr(0, idText.size() < 15 ? 15 - idText.size() : 0) + idText" "pool thread name within 15 characters")
string(FIND "${pool}" "sprintf(ThreadName, \"PoolThread %s %d\"" old_name)
if (NOT old_name EQUAL -1)
  message(FATAL_ERROR "#541: the over-long pool thread name is back (pthread_setname_np fails above 15 characters)")
endif()
require_text("${map_cpp}" "\"Cell\" + regionTag" "cell pool named by region")

message(STATUS "WORLD_TICK_541_CONTRACT=PASS")
