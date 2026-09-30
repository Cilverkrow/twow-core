if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#338 (train 8, owner 2026-09-30): 48 class-B registry rows leave the registry with a
# backup first, 89 become class A; loot tables are never touched.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260930140000_world.sql" migration)
string(REGEX REPLACE "--[^\n]*" "" statements "${migration}")

string(FIND "${statements}" "INSERT IGNORE INTO `creature_loot_bonus_registry_bak_338`" backup_at)
string(FIND "${statements}" "DELETE FROM `creature_loot_bonus_registry`" delete_at)
if (backup_at EQUAL -1 OR delete_at EQUAL -1 OR NOT backup_at LESS delete_at)
  message(FATAL_ERROR "Registry rows must be backed up before they are removed")
endif()

string(REGEX MATCHALL "[(][0-9]+,[0-9]+[)]" pairs "${statements}")
list(LENGTH pairs pair_count)
# 48 pairs in the backup insert + 48 in the delete + 89 in the class-A update
if (NOT pair_count EQUAL 185)
  message(FATAL_ERROR "Expected 48 + 48 + 89 = 185 pairs, found ${pair_count}")
endif()

foreach (forbidden "creature_loot_template" "reference_loot_template" "TRUNCATE" "DROP")
  string(FIND "${statements}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Registry cleanup must not touch ${forbidden}")
  endif()
endforeach()

# Sorcerer Ashcrombe (friendly NPC) is among the removed rows.
string(FIND "${statements}" "(3850,33)" ashcrombe)
if (ashcrombe EQUAL -1)
  message(FATAL_ERROR "Sorcerer Ashcrombe (3850/33) must leave the registry")
endif()

message(STATUS "REGISTRY_CLASSB_338_CONTRACT=PASS")
