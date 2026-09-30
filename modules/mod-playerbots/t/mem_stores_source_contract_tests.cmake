function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
file(READ "${CORE_SOURCE_DIR}/src/game/Maps/Map.cpp" map_cpp)

# #416 (7.3): hourly [MemStores] diagnostic, map list under the MapManager lock.
require_text("${mgr}" "[MemStores] rss_kb=%llu" "[MemStores] line")
require_text("${mgr}" "MaNGOS::ClassLevelLockable<MapManager, std::recursive_mutex>::Lock guard(sMapMgr);" "map list read under the MapManager lock")
require_text("${mgr}" "ai::mem_stores::Due(lastReport, now)" "hourly rate limit")
require_text("${map_cpp}" "++m_loadedGridCount;" "loaded grids counted")
require_text("${map_cpp}" "i_grids[idx][j] = nullptr;" "grid array initialised without the counter")
string(FIND "${map_cpp}" "setNGrid(nullptr, idx, j);" ctor_count)
if(NOT ctor_count EQUAL -1)
  message(FATAL_ERROR "Map constructor must not count its initial grids through setNGrid")
endif()

# #416 (7.3): value caches counted on the bot's thread, only summed on the world thread.
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
require_text("${ai_cpp}" "aiObjectContext->GetCreatedValueCounts();" "value count on the bot thread")
require_text("${ai_cpp}" "if (now - lastValueNamesTime >= ai::mem_stores::IntervalSeconds)" "value names at most once per hour")
require_text("${mgr}" "bot_values=%llu bot_values_avg=%u bot_values_max=%u shared_values=%u" "value-cache fields in [MemStores]")

# #452: allocator fields and live counts for Items, Loot and travel path points.
file(READ "${CORE_SOURCE_DIR}/src/game/Objects/Item.h" item_h)
file(READ "${CORE_SOURCE_DIR}/src/game/LootMgr.h" loot_h)
file(READ "${PB_SOURCE_DIR}/TravelNode.h" travel_h)
require_text("${mgr}" "struct mallinfo2 const info = mallinfo2();" "allocator totals")
require_text("${mgr}" "malloc_info(0, out);" "arena count")
require_text("${mgr}" " heap_kb=%llu inuse_kb=%llu free_kb=%llu arenas=%u items=%lld loots=%lld path_points=%lld" "#452 fields in [MemStores]")
require_text("${item_h}" "TrackedCount<Item> m_liveCount{1};" "live Items counted")
require_text("${loot_h}" "TrackedCount<Loot> m_liveCount{1};" "live Loot counted")
require_text("${travel_h}" "pathPoints.Set(int64(path.size()));" "travel path points counted")
require_text("${travel_h}" "setPath(basePath->path);" "copied travel paths counted")
