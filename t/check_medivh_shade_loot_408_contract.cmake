if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#408 (train 8): only creature_template.loot_id of 59994, 60063 and 60064 changes,
# guarded by the old value; the Echo of Medivh (61958, loot 2000213) keeps the epics.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260929211500_world.sql" migration)

foreach (required
    "UPDATE `creature_template` SET `loot_id` = 0 WHERE `entry` = 59994 AND `loot_id` = 59994;"
    "UPDATE `creature_template` SET `loot_id` = 0 WHERE `entry` IN (60063, 60064) AND `loot_id` = 2000213;")
  string(FIND "${migration}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing guarded update: ${required}")
  endif()
endforeach()

# Statements only (comments carry the rollback): no other table, no delete, Echo untouched.
string(REGEX REPLACE "--[^\n]*" "" statements "${migration}")
foreach (forbidden "DELETE" "INSERT" "creature_loot_template" "61958")
  string(FIND "${statements}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Medivh loot migration must only clear three loot ids, found: ${forbidden}")
  endif()
endforeach()

message(STATUS "MEDIVH_SHADE_LOOT_408_CONTRACT=PASS")
