cmake_policy(VERSION 3.16)
if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#575 (PATH-01 b, main train): bot flight links get this realm's TaxiPath ids (M1, 270 guarded UPDATEs +
# 6 path points of 1063->1420) and the stale ship 176310 leaves the bot graph (M2, owner 11.10.2026 "yes loeschen",
# 2 links + 34 path points, backup first). db-init re-creates the playerbot tables from the module base dump after the
# migrations, so the dump must carry the same end state.
# 1. Both files: LF, issue, Rollback, table guard by DISTINCT table names (temporary shadows do not double-count),
#    end-state CHECK.
# 2. M1: exactly 270 mapping tuples, the UPDATE guarded by the old id and both node positions; no DELETE.
# 3. M2: only if `transports` has no 176310; backup before the deletes; deletes only the backed-up 176310 rows.
# 4. Base dump = migrated end state: 270 flight links whose ids equal the M1 mapping (none above 360), no 176310 link,
#    no path row of 1196<->1197, the 6 F2 points at their new values.
set(dir "${TW_CORE_ROOT}/sql/database_updates")
set(m1 "${dir}/20261011160000_world.sql")
set(m2 "${dir}/20261011160100_world.sql")
set(dump "${TW_CORE_ROOT}/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql")

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#575 ${label}: missing ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#575 ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# SQL without comment lines.
function(strip_comments text out)
  string(REGEX REPLACE "(^|\n)--[^\n]*" "\\1" body "${text}")
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

foreach (f m1 m2)
  if (NOT EXISTS "${${f}}")
    message(FATAL_ERROR "#575: missing ${${f}}")
  endif()
  file(READ "${${f}}" text)
  # CR check on the raw bytes (a text read on a Windows host drops the CR).
  file(READ "${${f}}" hex HEX)
  if (hex MATCHES "^(..)*0d")
    message(FATAL_ERROR "#575 ${f}: CRLF")
  endif()
  require_text("${text}" "twow-repo#575" "${f} issue")
  require_text("${text}" "-- Rollback (exact, mangosd stopped):" "${f} rollback")
  require_text("${text}" "(SELECT COUNT(DISTINCT `TABLE_NAME`) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE()" "${f} table guard")
  require_text("${text}" "TINYINT(1) NOT NULL CHECK (`ok` = 1)" "${f} end-state check")
  strip_comments("${text}" body)
  string(TOUPPER "${body}" upper)
  foreach (forbidden "DROP TABLE " "TRUNCATE" "REPLACE INTO" "ALTER TABLE")
    string(FIND "${upper}" "${forbidden}" at)
    if (NOT at EQUAL -1)
      message(FATAL_ERROR "#575 ${f}: forbidden ${forbidden}")
    endif()
  endforeach()
  set(${f}_body "${body}")
endforeach()

# 2. M1
# A ";" would split the CMake list, so the statement end is flattened before matching.
string(REPLACE ";" "," m1_flat "${m1_body}")
string(REGEX MATCHALL "\n  \\(([0-9]+),([0-9]+),([0-9]+),([0-9]+),[-0-9.]+,[-0-9.]+,[-0-9.]+,[-0-9.]+\\)," tuples "${m1_flat}")
list(LENGTH tuples n_tuples)
if (NOT n_tuples EQUAL 270)
  message(FATAL_ERROR "#575 M1: expected 270 mapping tuples, found ${n_tuples}")
endif()
require_text("${m1_body}" "JOIN `ai_playerbot_travelnode` a ON a.`id` = l.`node_id` AND ABS(a.`x` - m.`fx`) < 0.5 AND ABS(a.`y` - m.`fy`) < 0.5" "M1 start node guard")
require_text("${m1_body}" "JOIN `ai_playerbot_travelnode` b ON b.`id` = l.`to_node_id` AND ABS(b.`x` - m.`tx`) < 0.5 AND ABS(b.`y` - m.`ty`) < 0.5" "M1 end node guard")
require_text("${m1_body}" "WHERE l.`type` = 4 AND l.`object` = m.`old_object`" "M1 old id guard")
string(TOUPPER "${m1_body}" m1_upper)
string(FIND "${m1_upper}" "DELETE" m1_delete)
if (NOT m1_delete EQUAL -1)
  message(FATAL_ERROR "#575 M1: no DELETE in the id fix")
endif()
# mapping (node,to) -> new id
set(map_keys "")
foreach (t IN LISTS tuples)
  string(REGEX MATCH "\\(([0-9]+),([0-9]+),([0-9]+),([0-9]+)," _ "${t}")
  set(new_${CMAKE_MATCH_1}_${CMAKE_MATCH_2} "${CMAKE_MATCH_4}")
endforeach()

# 3. M2
require_text("${m2_body}" "SET @ps_ship := (SELECT COUNT(*) FROM `transports` WHERE `entry` = 176310);" "M2 ship guard")
require_text("${m2_body}" "SET @ps_do := (@ps_tables = 3 AND @ps_ship = 0);" "M2 do flag")
require_order("${m2_body}" "INSERT IGNORE INTO `ai_playerbot_travelnode_link_bak_575`" "DELETE p FROM `ai_playerbot_travelnode_path` p" "M2 backup before delete")
require_order("${m2_body}" "INSERT IGNORE INTO `ai_playerbot_travelnode_path_bak_575`" "DELETE p FROM `ai_playerbot_travelnode_path` p" "M2 path backup before delete")
require_text("${m2_body}" "WHERE l.`type` = 3 AND l.`object` = 176310
      AND ((l.`node_id` = 1196 AND l.`to_node_id` = 1197) OR (l.`node_id` = 1197 AND l.`to_node_id` = 1196))" "M2 exact target")
require_text("${m2_body}" "JOIN `ai_playerbot_travelnode_link_bak_575` k ON k.`node_id` = l.`node_id` AND k.`to_node_id` = l.`to_node_id`
    WHERE l.`type` = 3 AND l.`object` = 176310" "M2 deletes only backed-up 176310 links")
string(REGEX MATCHALL "DELETE [a-z] FROM" deletes "${m2_body}")
list(LENGTH deletes n_deletes)
if (NOT n_deletes EQUAL 2)
  message(FATAL_ERROR "#575 M2: expected exactly 2 DELETE statements, found ${n_deletes}")
endif()

# 4. Base dump = end state
file(STRINGS "${dump}" flights REGEX "^\t\\([0-9]+, [0-9]+, 4, [0-9]+, [^,]+, [^,]+, [^,]+, [^,]+, [^,]+, [^,]+, [^,]+\\)[,;]$")
list(LENGTH flights n_flights)
if (NOT n_flights EQUAL 270)
  message(FATAL_ERROR "#575 dump: expected 270 flight links, found ${n_flights}")
endif()
foreach (row IN LISTS flights)
  string(REGEX MATCH "\\(([0-9]+), ([0-9]+), 4, ([0-9]+)," hit "${row}")
  if (NOT hit)
    message(FATAL_ERROR "#575 dump: unreadable flight row [${row}] (a row ending its INSERT block splits the list)")
  endif()
  set(key "new_${CMAKE_MATCH_1}_${CMAKE_MATCH_2}")
  if (NOT DEFINED ${key})
    message(FATAL_ERROR "#575 dump: flight link ${CMAKE_MATCH_1}->${CMAKE_MATCH_2} not in the M1 mapping")
  endif()
  if (NOT CMAKE_MATCH_3 EQUAL ${${key}})
    message(FATAL_ERROR "#575 dump: flight link ${CMAKE_MATCH_1}->${CMAKE_MATCH_2} has id ${CMAKE_MATCH_3}, M1 says ${${key}}")
  endif()
  if (CMAKE_MATCH_3 GREATER 360)
    message(FATAL_ERROR "#575 dump: flight id ${CMAKE_MATCH_3} is not in TaxiPath.dbc v8")
  endif()
endforeach()
file(STRINGS "${dump}" stale REGEX "^\t\\((1196, 1197|1197, 1196), ")
list(LENGTH stale n_stale)
if (NOT n_stale EQUAL 0)
  message(FATAL_ERROR "#575 dump: ${n_stale} rows of the stale ship 176310 (1196<->1197) left")
endif()
file(STRINGS "${dump}" f2 REGEX "^\t\\(1063, 1420, 1[0-5], ")
foreach (point "(1063, 1420, 10, 1, 2472.52, -3697.24, 70)" "(1063, 1420, 11, 1, 2580, -3720, 105)" "(1063, 1420, 15, 1, 2730, -3875, 103)")
  string(FIND "${f2}" "${point}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#575 dump: F2 point missing ${point}")
  endif()
endforeach()
message(STATUS "FLIGHT_IDS_575_CONTRACT=PASS")
