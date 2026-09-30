if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

# twow-repo#338 (train 8, owner 2026-09-30): everything hostile in instances, bosses included,
# respawns after at least 604800 s (weekly id reset); friendly and event NPCs keep their times.
# Only instance maps, values only go up, originals kept once in creature_bak_338.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260930120000_world.sql" migration)
string(REGEX REPLACE "--[^\n]*" "" statements "${migration}")

foreach (required
    "CREATE TABLE IF NOT EXISTS `creature_bak_338`"
    "INSERT IGNORE INTO `creature_bak_338`"
    "m.`map_type` IN (1, 2)"
    "c.`spawntimesecsmin` > 0"
    "k.`old_max`, 604800"
    "k.`kind` = 'boss' OR k.`faction` NOT IN (12, 23, 35, 55, 68, 80, 113, 122, 534, 714, 875, 1608)"
    "c.`id` NOT IN (50105, 91931, 3850, 15378, 15379, 15380)"
    "GREATEST(b.`old_min`, b.`target`)"
    "GREATEST(b.`old_max`, b.`target`)"
    "UPDATE `map_template` SET `reset_delay` = 7 WHERE `entry` = 819 AND `reset_delay` = 0")
  require_text("${statements}" "${required}" "instance respawn rule")
endforeach()

# No stepped trash value any more (owner 2026-09-30: trash also 7 days).
string(FIND "${statements}" "9000" stepped)
if (NOT stepped EQUAL -1)
  message(FATAL_ERROR "The 9000 s trash step was replaced by 604800 s")
endif()

foreach (forbidden "DELETE" "DROP" "TRUNCATE" "REPLACE INTO" "gameobject")
  string(FIND "${statements}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Instance respawn migration must not contain ${forbidden}")
  endif()
endforeach()

require_text("${migration}" "-- Rollback:" "documented rollback")
message(STATUS "INSTANCE_RESPAWN_338_CONTRACT=PASS")
