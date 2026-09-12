if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${label}: ${needle}")
  endif()
endfunction()

file(READ "${TW_CORE_ROOT}/sql/database_updates/20260912120000_world.sql" migration)
file(READ "${TW_CORE_ROOT}/modules/mod-dungeon-clear/data/canonical-boss-loot-seed.sql" canonical)
if (NOT migration STREQUAL canonical)
  message(FATAL_ERROR "Migration is not byte-identical to the canonical 136-row seed")
endif()

file(READ "${TW_CORE_ROOT}/src/game/LootMgr.cpp" loot)
file(READ "${TW_CORE_ROOT}/src/game/ObjectMgr.cpp" object_mgr)
file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" config)
foreach (required
    "ProcessBonus" "item.needs_quest" "ITEM_CLASS_KEY" "ITEM_CLASS_RECIPE"
    "ITEM_FLAG_UNIQUE_EQUIPPED" "MAX_NR_LOOT_ITEMS" "group.ProcessBonus")
  require_text("${loot}" "${required}" "loot contract")
endforeach()
require_text("${object_mgr}" "creature_loot_bonus_registry" "canonical registry query")
forbid_text("${object_mgr}" "funserver_loot_bonus_boss" "obsolete registry")
foreach (required
    "Funserver.Loot.Bonus.Enabled = 0" "Funserver.Loot.Bonus.Rare = 0"
    "Funserver.Loot.Bonus.RareElite = 0" "Funserver.Loot.Bonus.WorldBoss = 0"
    "Funserver.Loot.Bonus.DungeonBoss = 0" "Funserver.Loot.Bonus.RaidBoss = 0")
  require_text("${config}" "${required}" "default-disabled configuration")
endforeach()
message(STATUS "LOOT_BONUS_SOURCE_CONTRACT=PASS")
