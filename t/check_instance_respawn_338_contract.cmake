if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

# twow-repo#338 (train 8, owner 2026-09-30): instance trash respawns after at least 9000 s,
# bosses and rares never within an instance id (604800 s). Only instance maps, values only go
# up, originals kept once in creature_bak_338 (replay-safe, reversible).
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260930120000_world.sql" migration)
string(REGEX REPLACE "--[^\n]*" "" statements "${migration}")

foreach (required
    "CREATE TABLE IF NOT EXISTS `creature_bak_338`"
    "INSERT IGNORE INTO `creature_bak_338`"
    "m.`map_type` IN (1, 2)"
    "c.`spawntimesecsmin` > 0"
    "IF(k.`kind` = 'trash', 9000, 604800)"
    "WHEN t.`rank` IN (2, 4) THEN 'rare'"
    "r.`note` NOT LIKE '%class B%'"
    "GREATEST(b.`old_min`, b.`target`)"
    "GREATEST(b.`old_max`, b.`target`)")
  require_text("${statements}" "${required}" "instance respawn rule")
endforeach()

foreach (forbidden "DELETE" "DROP" "TRUNCATE" "REPLACE INTO" "gameobject")
  string(FIND "${statements}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Instance respawn migration must not contain ${forbidden}")
  endif()
endforeach()

require_text("${migration}" "-- Rollback:" "documented rollback")
message(STATUS "INSTANCE_RESPAWN_338_CONTRACT=PASS")
