if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Train 9 (owner 2026-10-01, "abschalten, nicht löschen"):
#   20261001120000: twow-repo#427 map-44 GM marks redirected, #461 116 spawns without client
#                   terrain and the excavation tent GO 4004476 disabled (SPAWN_FLAG_DISABLED);
#                   backups first, nothing deleted.
#   20261001121000: twow-repo#459 Frostmane Hollow: game_tele 811, Oboka's Axe at Oboka,
#                   one ability for Warrior/Mercenary/Ritualist/Ubukaz.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20261001120000_world.sql" m1)
file(READ "${TW_CORE_ROOT}/sql/database_updates/20261001121000_world.sql" m2)
string(REGEX REPLACE "--[^\n]*" "" s1 "${m1}")
string(REGEX REPLACE "--[^\n]*" "" s2 "${m2}")

foreach (forbidden "DELETE " "DROP " "TRUNCATE" "REPLACE " "`gameobject_template`")
  string(FIND "${s1}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "#427/#461 only switch content off, found: ${forbidden}")
  endif()
endforeach()
foreach (forbidden "DELETE " "DROP " "TRUNCATE" "REPLACE " "WHERE `entry` = 63133")
  string(FIND "${s2}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "#459 must stay insert/guarded-update only, found: ${forbidden}")
  endif()
endforeach()

# Backups strictly before the switch-off statements.
macro(require_before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "${what}: backup must exist and come first")
  endif()
endmacro()
require_before("${s1}" "INSERT IGNORE INTO `game_tele_bak_427`" "UPDATE `game_tele`" "#427 game_tele")
require_before("${s1}" "INSERT IGNORE INTO `creature_bak_ws30_terrain`" "UPDATE `creature` c" "#461 creature")
require_before("${s1}" "INSERT IGNORE INTO `gameobject_bak_ws30_terrain`" "UPDATE `gameobject` g" "#461 gameobject")
require_before("${s2}" "INSERT IGNORE INTO `game_tele_bak_459`" "UPDATE `game_tele`" "#459 game_tele")
require_before("${s2}" "INSERT IGNORE INTO `creature_template_bak_459`" "UPDATE `creature_template`" "#459 creature_template")

# #461: exactly the 116 listed Rabbit spawns, disabled by flag only.
string(REGEX MATCH "`guid` IN \\(([0-9,\n ]+)\\)" guid_block "${s1}")
string(REGEX MATCHALL "[0-9]+" guids "${CMAKE_MATCH_1}")
list(LENGTH guids guid_count)
if (NOT guid_count EQUAL 116)
  message(FATAL_ERROR "#461: expected 116 spawn guids, found ${guid_count}")
endif()
foreach (required
    "WHERE `map` = 0 AND `id` = 721 AND `guid` IN ("
    "SET c.`spawn_flags` = c.`spawn_flags` | 2;"
    "WHERE `id` IN (621, 622, 809, 810, 827) AND `map` = 44;")
  string(FIND "${s1}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing #427/#461 statement: ${required}")
  endif()
endforeach()

# #459: the approved targets.
foreach (required
    "SET `map` = 822, `position_x` = -7522.73, `position_y` = -3588.76, `position_z` = 199.981, `orientation` = 2.2022"
    "WHERE `id` = 811 AND `map` = 806;"
    "VALUES (63132, 184, 33.33, 0, 1, 1, 0);"
    "UPDATE `creature_template` SET `loot_id` = 63132 WHERE `entry` = 63132 AND `loot_id` = 0;"
    "(108,   'Frostmane Hollow - Frostmane Warrior',     11971,"
    "(19,    'Frostmane Hollow - Undermarket Mercenary', 8242,"
    "(96,    'Frostmane Hollow - Frostmane Ritualist',   1108,"
    "(63131, 'Frostmane Hollow - Battlemaster Ubukaz',   15496,"
    "WHERE `entry` IN (19, 96, 108, 63131) AND `spell_list_id` = 0;"
    "VALUES (63132, 822, 'dungeon', '#459 owner 2026-10-01 boss list (Handler Oboka)');")
  string(FIND "${s2}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing #459 statement: ${required}")
  endif()
endforeach()
# One ability per creature: each list fills slot 1 only.
string(FIND "${s2}" "spellId_2" second_slot)
if (NOT second_slot EQUAL -1)
  message(FATAL_ERROR "#459: owner rule allows one special ability per trash type")
endif()

message(STATUS "WORLD_DISABLE_ZUG9_CONTRACT=PASS")
