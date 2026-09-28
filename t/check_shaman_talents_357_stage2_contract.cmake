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

# twow-repo#357 stage 2 (client talents, twow-repo#409): weapon talent W 90130-90139,
# shaman sword skills via skill_race_class_info_mod, talent icons. Replay-safe.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260928200000_world.sql" m)
string(REGEX MATCHALL "INTO `spell_template` SELECT [*] FROM `tmp_spell`" clones "${m}")
list(LENGTH clones clone_count)
if (NOT clone_count EQUAL 10)
  message(FATAL_ERROR "#357 stage 2: expected 10 spell clones (90130-90139), got ${clone_count}")
endif()
foreach (id RANGE 90130 90139)
  require_text("${m}" "SET `entry` = ${id}," "#357 stage 2 W spell")
endforeach()
# The phase-1 rank spells are reused, never cloned again.
foreach (id RANGE 90100 90129)
  forbid_text("${m}" "SET `entry` = ${id}," "#357 stage 2 re-clone of a phase-1 rank spell")
endforeach()
foreach (required
    # W rank: learns both sword skills and the hub (shape of 16269).
    "WHERE `entry` = 16269;"
    "`effectTriggerSpell1` = 201, `effectTriggerSpell2` = 202"
    "`effect3` = 36"
    "`effectTriggerSpell3` = 90131"
    # Hubs add the leaves (aura 192).
    "`effectApplyAuraName1` = 192"
    "`effectTriggerSpell1` = 90135"
    "`effectTriggerSpell2` = 90139"
    # O-18: mace +5, two-handed mace +10, dagger +5 weapon skill.
    "`effectBasePoints1` = 4, `effectBonusCoefficient1` = -1, `effectMechanic1` = 0, `effectImplicitTargetA1` = 1, `effectImplicitTargetB1` = 0, `effectRadiusIndex1` = 0, `effectApplyAuraName1` = 98, `effectAmplitude1` = 0, `effectMultipleValue1` = 0, `effectChainTarget1` = 0, `effectItemType1` = 0, `effectMiscValue1` = 54"
    "`effectApplyAuraName2` = 98, `effectAmplitude2` = 0, `effectMultipleValue2` = 0, `effectChainTarget2` = 0, `effectItemType2` = 0, `effectMiscValue2` = 160"
    "`effectMiscValue3` = 173"
    # Swords 5 % / 10 % extra attack, axes 4 % / 8 %, daggers 5 % crit (O-17, O-19).
    "`equippedItemSubClassMask` = 128, `procChance` = 5"
    "`equippedItemSubClassMask` = 256, `procChance` = 10"
    "`equippedItemSubClassMask` = 1, `effectBasePoints1` = 3"
    "`equippedItemSubClassMask` = 2, `effectBasePoints1` = 7"
    "`equippedItemSubClassMask` = 32768, `effectBasePoints1` = 4"
    # Sword skills: shaman only, talent-gated like SkillRaceClassInfo rows 701/702.
    "(90043, 43, 2047, 64, 384, 0, 0, -1,"
    "(90055, 55, 2047, 64, 384, 0, 0, -1,")
  require_text("${m}" "${required}" "#357 stage 2 migration")
endforeach()
# Existing spells: icons of the phase-1 rank spells and the Elemental Weapons tooltips.
string(REGEX MATCHALL "UPDATE `spell_template` SET `spellIconId` = [0-9]+ +WHERE `entry` (BETWEEN 901[0-2][0-9] AND 901[0-2][0-9]|= 901[0-2][0-9])" icon_updates "${m}")
list(LENGTH icon_updates icon_update_count)
string(REGEX MATCHALL "UPDATE `spell_template`" live_updates "${m}")
list(LENGTH live_updates live_update_count)
if (NOT icon_update_count EQUAL 7 OR NOT live_update_count EQUAL 10)
  message(FATAL_ERROR "#357 stage 2: expected 7 icon UPDATEs on 90100-90129 plus 3 tooltip UPDATEs, got ${icon_update_count} of ${live_update_count}")
endif()
# Elemental Weapons tooltips follow the code cap (build % x 4/3, core#182).
foreach (rank_text
    "'cannot exceed 13% of maximum health') WHERE `entry` = 16266;"
    "'cannot exceed 27% of maximum health') WHERE `entry` = 29079;"
    "'cannot exceed 40% of maximum health') WHERE `entry` = 29080;")
  require_text("${m}" "${rank_text}" "#357 stage 2 Earthen Bulwark tooltip")
endforeach()
# Weapon masters keep refusing swords to shamans: skill_line_ability and trainers untouched.
foreach (forbidden "DELETE " "REPLACE "
    "INTO `npc_trainer`" "UPDATE `npc_trainer`"
    "INTO `skill_line_ability`" "UPDATE `skill_line_ability`"
    "INTO `spell_learn_spell`" "UPDATE `spell_learn_spell`")
  forbid_text("${m}" "${forbidden}" "#357 stage 2 scope")
endforeach()
message(STATUS "SHAMAN_TALENTS_357_STAGE2_CONTRACT=PASS")
