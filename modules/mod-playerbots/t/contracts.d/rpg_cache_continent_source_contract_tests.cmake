cmake_policy(VERSION 3.16)
# RPG teleport cache is built from continent spawns only (OB-00 21.11.2026, OB-20 ship/transport check, DS4 finding): the creature x
# ai_playerbot_rpg_races join had no map filter, so entry 7733 (Innkeeper Fizzgrimble, also spawned on map 451 = Programmer Isle, the GM
# island) put the GM island into the level 40-50 teleport targets of every race. Fix: AND c.map IN (0, 1) (= MapEntry::IsContinent,
# Map.h). Pins the filter in the query and that the old unfiltered WHERE is gone.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "rpg cache continent: PB_SOURCE_DIR is required")
endif()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" text)
string(REPLACE "\r" "" text "${text}")

function(require_text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "rpg cache continent: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "rpg cache continent: forbidden ${description}: ${needle}")
  endif()
endfunction()

# The query and its map filter stay together: the join, the race filter, then the continent filter in the same statement.
string(FIND "${text}" "FROM creature c INNER JOIN ai_playerbot_rpg_races r ON c.id = r.entry" join_at)
if(join_at EQUAL -1)
  message(FATAL_ERROR "rpg cache continent: missing the creature x ai_playerbot_rpg_races join")
endif()
string(SUBSTRING "${text}" ${join_at} 200 query)
string(FIND "${query}" "WHERE r.race < 15 AND c.map IN (0, 1)\");" where_at)
if(where_at EQUAL -1)
  message(FATAL_ERROR "rpg cache continent: the join is not followed by 'WHERE r.race < 15 AND c.map IN (0, 1)' (GM island spawn of entry 7733)")
endif()

forbid_text("\"WHERE r.race < 15\");" "the unfiltered RPG cache query (map 451 spawn of entry 7733)")
require_text("MapEntry::IsContinent" "the pointer to the continent rule in the comment")
