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
