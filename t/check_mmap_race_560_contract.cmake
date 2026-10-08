if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#560 (R1-R3, OB-00 go 09.10.2026): MMapManager reads its tables only under their locks.
# loadedMMaps / loadedModels are std::unordered_map; a find while another thread inserts (rehash) is
# undefined behaviour. Every access holds loadedMMaps_lock / loadedModels_lock (shared to read,
# unique to insert or erase), per-thread queries navMeshQueries_lock. No operator[] on the tables
# (it inserts). Stability only: no behaviour change. R4 (addTile vs. queries) is a separate step.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#560: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#560: ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#560: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Body of one function: from its signature to the first closing brace at column 0.
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "#560: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if (end EQUAL -1)
    message(FATAL_ERROR "#560: end of function not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("src/game/Maps/MoveMap.h" mm_h)
read_source("src/game/Maps/MoveMap.cpp" mm_cpp)

require_text("${mm_h}" "std::shared_mutex loadedModels_lock;" "lock of the model table")
forbid_text("${mm_h}" "lockForModels" "the model table lock replaces lockForModels")

# No operator[] on the tables anywhere (it inserts, also under a shared lock).
forbid_text("${mm_cpp}" "loadedMMaps[" "operator[] on loadedMMaps")
forbid_text("${mm_cpp}" "loadedModels[" "operator[] on loadedModels")
forbid_text("${mm_cpp}" "navMeshQueries[" "operator[] on navMeshQueries")

set(shared_maps "std::shared_lock<std::shared_mutex> rlock(loadedMMaps_lock);")
set(unique_maps "std::unique_lock<std::shared_mutex> wlock(loadedMMaps_lock);")
set(shared_models "std::shared_lock<std::shared_mutex> rlock(loadedModels_lock);")
set(unique_models "std::unique_lock<std::shared_mutex> wlock(loadedModels_lock);")

# R1: readers of loadedMMaps.
function_body("${mm_cpp}" "dtNavMesh const* MMapManager::GetNavMesh(uint32 mapId)" body)
require_order("${body}" "${shared_maps}" "loadedMMaps.find(mapId)" "GetNavMesh")

function_body("${mm_cpp}" "dtNavMeshQuery const* MMapManager::GetNavMeshQuery(uint32 mapId)" body)
require_order("${body}" "${shared_maps}" "loadedMMaps.find(mapId)" "GetNavMeshQuery")
require_text("${body}" "QueryForThread(mmap, \"mapId\", mapId)" "per-thread query")

function_body("${mm_cpp}" "bool MMapManager::loadMap(uint32 mapId, int32 x, int32 y)" body)
require_order("${body}" "${shared_maps}" "loadedMMaps.find(mapId)" "loadMap")

function_body("${mm_cpp}" "bool MMapManager::loadMapData(uint32 mapId)" body)
require_order("${body}" "${unique_maps}" "loadedMMaps.insert(" "loadMapData insert")

# R2: unload paths (gated by MMapTileUnload, locked anyway).
function_body("${mm_cpp}" "bool MMapManager::unloadMap(uint32 mapId, int32 x, int32 y)" body)
require_order("${body}" "${shared_maps}" "loadedMMaps.find(mapId)" "unloadMap tile")
require_order("${body}" "std::unique_lock<std::mutex> tilesLock(mmap->tilesLoading_lock);" "mmap->navMesh->removeTile(" "tile set under tilesLoading_lock")

function_body("${mm_cpp}" "bool MMapManager::unloadMap(uint32 mapId)" body)
require_order("${body}" "${unique_maps}" "loadedMMaps.find(mapId)" "unloadMap map")
require_order("${body}" "loadedMMaps.erase(loaded);" "delete mmap;" "erase before delete")

function_body("${mm_cpp}" "bool MMapManager::unloadMapInstance(uint32 mapId, std::thread::id instanceId)" body)
require_order("${body}" "${shared_maps}" "loadedMMaps.find(mapId)" "unloadMapInstance table")
require_order("${body}" "std::unique_lock<std::shared_mutex> queriesLock(mmap->navMeshQueries_lock);" "mmap->navMeshQueries.erase(query);" "query erase")

# R1/R3: per-thread queries.
function_body("${mm_cpp}" "dtNavMeshQuery const* MMapManager::QueryForThread(MMapData* mmap, char const* kind, uint32 id)" body)
require_order("${body}" "std::shared_lock<std::shared_mutex> lock(mmap->navMeshQueries_lock);" "mmap->navMeshQueries.find(tid)" "query lookup")
require_order("${body}" "std::unique_lock<std::shared_mutex> ulock(mmap->navMeshQueries_lock);" "mmap->navMeshQueries.insert(" "query insert")

# R3: model table.
function_body("${mm_cpp}" "bool MMapManager::loadGameObject(uint32 displayId)" body)
require_order("${body}" "${shared_models}" "loadedModels.find(displayId)" "loadGameObject check")
require_order("${body}" "${unique_models}" "loadedModels.insert(" "loadGameObject insert")

function_body("${mm_cpp}" "dtNavMeshQuery const* MMapManager::GetModelNavMeshQuery(uint32 displayId)" body)
require_order("${body}" "${shared_models}" "loadedModels.find(displayId)" "GetModelNavMeshQuery")
require_text("${body}" "QueryForThread(mmap, \"displayid\", displayId)" "per-thread model query")

message(STATUS "mmap_race_560 contract passed")
