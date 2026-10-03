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

# twow-repo#357 route B: bot-only shaman talent auras 90100-90129, owner values, replay-safe.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927170000_world.sql" m)
string(REGEX MATCHALL "INTO `spell_template` SELECT [*] FROM `tmp_spell`" clones "${m}")
list(LENGTH clones clone_count)
if (NOT clone_count EQUAL 29)
  message(FATAL_ERROR "#357 route B: expected 29 spell clones (90100-90129 without 90110), got ${clone_count}")
endif()
foreach (id RANGE 90100 90129)
  if (id EQUAL 90110)
    # Ghost Wolf instant comes from Improved Ghost Wolf rank 2 for everyone (owner, 2026-09-27).
    forbid_text("${m}" "SET `entry` = ${id}," "#357 route B unused id")
  else()
    require_text("${m}" "SET `entry` = ${id}," "#357 route B spell")
  endif()
endforeach()
foreach (required
    "`effectBasePoints1` = 9, `effect2` = 0"
    "`effectBasePoints1` = 29"
    "UPDATE `spell_template` SET `effectBasePoints1` = -3001 WHERE `entry` = 16287;"
    "`effectApplyAuraName1` = 108, `effectMiscValue1` = 8, `effectItemType1` = 31457280, `effectBasePoints1` = 8"
    "`procChance` = 90, `effectApplyAuraName1` = 42"
    "`procChance` = 100, `effectApplyAuraName1` = 42"
    "`procCharges` = 1, `stackAmount` = 5, `durationIndex` = 9, `effectMiscValue1` = 10, `effectBasePoints1` = -21"
    "`effectMiscValue2` = 14, `effectBasePoints2` = -21"
    "`effectApplyAuraName1` = 137, `effectMiscValue1` = 2, `effectBasePoints1` = 2"
    "`effectApplyAuraName1` = 87, `effectMiscValue1` = 127, `effectBasePoints1` = -3"
    "(90116, 0, 0, 0, 0, 0, 0, 112, 0, 0, 1)"
    "(90122, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0)"
    "WHERE `entry` = 17364 AND `script_name` = '';")
  require_text("${m}" "${required}" "#357 route B migration")
endforeach()
# Only the Stormstrike script binding and Improved Ghost Wolf rank 2 touch existing spells;
# nothing is deleted or replaced.
string(REGEX MATCHALL "UPDATE `spell_template`" live_updates "${m}")
list(LENGTH live_updates live_update_count)
if (NOT live_update_count EQUAL 2)
  message(FATAL_ERROR "#357 route B: expected exactly 2 UPDATEs of spell_template, got ${live_update_count}")
endif()
foreach (forbidden "DELETE " "REPLACE " "npc_trainer" "skill_line_ability")
  forbid_text("${m}" "${forbidden}" "#357 route B scope")
endforeach()

# Train 9 (twow-repo#484): Charged Stormstrike has 4 ranks; rank r consumes up to r charges
# (check_shaman_talents_484_contract.cmake covers the ranks in detail).
file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_shaman.cpp" script)
foreach (required
    "RegisterAuraScript(\"spell_shaman_retaliation\""
    "RegisterSpellScript(\"spell_shaman_stormstrike_charges\""
    "RegisterAuraScript(\"spell_shaman_storm_wisdom\""
    "RegisterAuraScript(\"spell_shaman_shield_charge_scaling\""
    "uint32 const CHARGED_STORMSTRIKE_RANKS[]      = { SPELL_SHAMAN_CHARGED_STORMSTRIKE_R1, SPELL_SHAMAN_CHARGED_STORMSTRIKE_R2,"
    "uint32 const STORMSTRIKE_PCT_PER_CHARGE       = 10;"
    "uint32 const RETALIATION_COOLDOWN_SECONDS     = 1;"
    "return std::min(GetLightningShieldCharges(caster), rank);")
  require_text("${script}" "${required}" "#357 route B script")
endforeach()
message(STATUS "SHAMAN_ROUTE_B_357_CONTRACT=PASS")
