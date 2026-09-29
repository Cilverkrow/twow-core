function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)

# #416 (7.3): no world creature scan per area at runtime; the startup load
# covers every area id.
require_text("${travel_mgr}" "ai::area_level::MayUseCreatureLevels(loadingAreaLevels)" "creature levels only at startup")
require_text("${travel_mgr}" "ai::area_level::AreaIdEnd(sAreaStore.GetMaxEntry(), sAreaStore.GetNumRows())" "full area id range")
require_text("${travel_mgr}" "std::lock_guard<std::recursive_mutex> lock(areaLevelMutex);" "area level lock")

# GetAreaLevel itself scans no creatures any more; the one pass lives in
# LoadCreatureAreaLevels, which directly follows it.
string(FIND "${travel_mgr}" "int32 TravelMgr::GetAreaLevel(uint32 area_id)" get_at)
string(FIND "${travel_mgr}" "void TravelMgr::LoadCreatureAreaLevels()" load_at)
string(FIND "${travel_mgr}" "void TravelMgr::LoadAreaLevels()" area_load_at)
if(get_at EQUAL -1 OR load_at LESS get_at OR area_load_at LESS load_at)
  message(FATAL_ERROR "Unexpected order of GetAreaLevel / LoadCreatureAreaLevels / LoadAreaLevels")
endif()
math(EXPR get_len "${load_at} - ${get_at}")
string(SUBSTRING "${travel_mgr}" ${get_at} ${get_len} get_body)
string(FIND "${get_body}" "getCreaturesNear" get_scan)
if(NOT get_scan EQUAL -1)
  message(FATAL_ERROR "GetAreaLevel must not scan the world's creatures")
endif()
math(EXPR load_len "${area_load_at} - ${load_at}")
string(SUBSTRING "${travel_mgr}" ${load_at} ${load_len} load_body)
require_text("${load_body}" "WorldPosition().getCreaturesNear()" "the one pass in LoadCreatureAreaLevels")
