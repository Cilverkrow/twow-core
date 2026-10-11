# twow-repo#541 (CLI-541 tail analysis, 11.10.2026): TravelNodeMap::getFullPath returned on an empty route without
# unlock_shared - a leaked reader per bot without a route, after which every writer of m_nMapMtx starved. The lock is
# RAII now; no manual lock/unlock pair in TravelNode.cpp may be unbalanced or left by a return.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR at "${at} + 1")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("TravelNode.cpp" travel)

string(FIND "${travel}" "TravelPath TravelNodeMap::getFullPath(" start)
if(start EQUAL -1)
  message(FATAL_ERROR "#541 travel node lock: getFullPath not found")
endif()
string(SUBSTRING "${travel}" ${start} -1 rest)
string(FIND "${rest}" "\n}\n" end)
string(SUBSTRING "${rest}" 0 ${end} full_path)

string(FIND "${full_path}" "std::shared_lock<std::shared_timed_mutex> nodeMapLock(sTravelNodeMap.m_nMapMtx);" raii)
if(raii EQUAL -1)
  message(FATAL_ERROR "#541 travel node lock: getFullPath must hold the node map with an RAII shared_lock")
endif()
foreach(manual "m_nMapMtx.lock_shared()" "m_nMapMtx.unlock_shared()")
  string(FIND "${full_path}" "${manual}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "#541 travel node lock: no manual ${manual} in getFullPath (a return path leaks it)")
  endif()
endforeach()
string(FIND "${full_path}" "if (route.isEmpty())" empty_at)
if(empty_at EQUAL -1 OR empty_at LESS raii)
  message(FATAL_ERROR "#541 travel node lock: the empty-route return must lie inside the RAII scope")
endif()

# Every manual shared lock in the file has its unlock.
count_text("${travel}" "m_nMapMtx.lock_shared()" shared_locks)
count_text("${travel}" "m_nMapMtx.unlock_shared()" shared_unlocks)
if(NOT shared_locks EQUAL shared_unlocks)
  message(FATAL_ERROR "#541 travel node lock: ${shared_locks} manual lock_shared vs ${shared_unlocks} unlock_shared in TravelNode.cpp")
endif()

message(STATUS "travel_node_lock source contract passed")
