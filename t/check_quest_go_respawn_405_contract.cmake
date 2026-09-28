if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#405 (owner, train 8): quest-item chests respawn in half the time, floor 5 s,
# replay-safe from stored originals, reversible.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260928120000_world.sql" m)
foreach (required
    "CREATE TABLE IF NOT EXISTS `gameobject_respawn_halving_405`"
    "INSERT IGNORE INTO `gameobject_respawn_halving_405`"
    "WHERE g.`spawntimesecsmin` > 0 AND g.`id` IN ("
    "SET g.`spawntimesecsmin` = LEAST(b.`old_min`, GREATEST(5, b.`old_min` DIV 2)),"
    "g.`spawntimesecsmax` = LEAST(b.`old_max`, GREATEST(5, b.`old_max` DIV 2));"
    "SET g.spawntimesecsmin = b.old_min, g.spawntimesecsmax = b.old_max;")
  string(FIND "${m}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing #405 statement: ${required}")
  endif()
endforeach()
# The entry list is fixed at the dry run: 555 quest-only chests.
string(FIND "${m}" "g.`id` IN (" list_at)
string(SUBSTRING "${m}" ${list_at} -1 list)
string(FIND "${list}" ");" list_end)
string(SUBSTRING "${list}" 0 ${list_end} list)
string(REGEX MATCHALL "[0-9]+" ids "${list}")
list(LENGTH ids id_count)
if (NOT id_count EQUAL 555)
  message(FATAL_ERROR "#405: expected 555 quest-item chest entries, got ${id_count}")
endif()
foreach (forbidden "DELETE " "REPLACE " "`creature`" "DROP TABLE")
  string(FIND "${m}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Forbidden #405 scope: ${forbidden}")
  endif()
endforeach()
message(STATUS "QUEST_GO_RESPAWN_405_CONTRACT=PASS")
