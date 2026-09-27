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

# twow-repo#367 rogue tank, bots only (D-6): owner values, nothing trained, sold or dropped.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927180000_world.sql" m)
string(REGEX MATCHALL "INTO `spell_template` SELECT [*] FROM `tmp_spell`" clones "${m}")
list(LENGTH clones clone_count)
if (NOT clone_count EQUAL 7)
  message(FATAL_ERROR "#367: expected 7 spell clones (90140-90146), got ${clone_count}")
endif()
foreach (id RANGE 90140 90146)
  require_text("${m}" "SET `entry` = ${id}," "#367 spell")
endforeach()
foreach (required
    "`recoveryTime` = 10000, `powerType` = 3, `manaCost` = 30, `rangeIndex` = 11, `spellLevel` = 12"
    "`effectBasePoints1` = 49,"
    "`effectBasePoints1` = 89,"
    "`effectBasePoints1` = 129,"
    "`durationIndex` = 27, `effectApplyAuraName1` = 49, `effectBasePoints1` = 4"
    "`durationIndex` = 27, `effectApplyAuraName1` = 47, `effectBasePoints1` = 4"
    "(90144, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0)"
    "SET `entry` = 90140, `required_level` = 20, `bonding` = 1, `buy_price` = 0, `sell_price` = 0;"
    "WHERE `entry` = 45613 AND `script_name` = '';")
  require_text("${m}" "${required}" "#367 migration")
endforeach()
# Bots only: no trainer, recipe, vendor or loot source; nothing deleted.
foreach (forbidden "npc_trainer" "npc_vendor" "loot_template" "skill_line_ability" "DELETE " "REPLACE ")
  forbid_text("${m}" "${forbidden}" "#367 scope")
endforeach()

file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_rogue.cpp" script)
foreach (required
    "{ 60, 395 },"
    "{ 50, 335 },"
    "{ 40, 275 },"
    "{ 30, 210 },"
    "{  0, 150 },"
    "uint32 const SPIT_EXTRA_TARGETS  = 2;"
    "RegisterSpellScript(\"spell_rogue_agitating_poison\""
    "RegisterSpellScript(\"spell_rogue_spit\""
    "RegisterAuraScript(\"spell_rogue_shadow_dance\"")
  require_text("${script}" "${required}" "#367 script")
endforeach()
message(STATUS "ROGUE_TANK_367_CONTRACT=PASS")
