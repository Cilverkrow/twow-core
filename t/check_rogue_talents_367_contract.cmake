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

# twow-repo#367: the owner's rogue talent line as bot auras 90150-90193, owner values.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927200000_world.sql" m)
string(REGEX MATCHALL "INTO `spell_template` SELECT [*] FROM `tmp_spell`" clones "${m}")
list(LENGTH clones clone_count)
if (NOT clone_count EQUAL 44)
  message(FATAL_ERROR "#367 talents: expected 44 spell clones (90150-90193), got ${clone_count}")
endif()
foreach (id RANGE 90150 90193)
  require_text("${m}" "SET `entry` = ${id}," "#367 talent spell")
endforeach()
foreach (required
    "Your attacks deal 20% more damage when you are behind the target.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 19"
    "`procFlags` = 20, `procChance` = 50, `effectApplyAuraName1` = 42"
    "`effectBasePoints1` = 29;"
    "`effectBasePoints1` = 32;"
    "`effectApplyAuraName1` = 137, `effectMiscValue1` = 1, `effectBasePoints1` = 9"
    "`effectApplyAuraName1` = 98, `effectMiscValue1` = 95, `effectBasePoints1` = 29"
    "`procFlags` = 680, `procChance` = 36, `effectApplyAuraName1` = 42"
    "`effectApplyAuraName1` = 22, `effectMiscValue1` = 126, `effectBasePoints1` = 44, `effect2` = 6, `effectApplyAuraName2` = 23, `effectMiscValue2` = 0, `effectBasePoints2` = 2, `effectAmplitude2` = 1000"
    "`effectApplyAuraName1` = 87, `effectMiscValue1` = 127, `effectBasePoints1` = -13, `effect2` = 6, `effectApplyAuraName2` = 142, `effectMiscValue2` = 1, `effectBasePoints2` = 11"
    "`effectBasePoints1` = 59;"
    "`procFlags` = 20, `procChance` = 100, `effectApplyAuraName1` = 42, `effectBasePoints1` = 23"
    "`stackAmount` = 10, `durationIndex` = 31, `effectApplyAuraName1` = 79, `effectMiscValue1` = 127, `effectBasePoints1` = 1"
    "`school` = 5, `dmgClass` = 0"
    "`stackAmount` = 4, `durationIndex` = 8"
    "(90171, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0)"
    "(90155, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0)"
    "(90190, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0)"
    "WHERE `entry` = 52526 AND `script_name` = '';"
    "WHERE `entry` = 14189 AND `script_name` = '';"
    "WHERE `entry` = 16511 AND `script_name` = '';")
  require_text("${m}" "${required}" "#367 talents migration")
endforeach()
foreach (forbidden "DELETE " "REPLACE " "npc_trainer" "skill_line_ability" "UPDATE `spell_template` SET `effect")
  forbid_text("${m}" "${forbidden}" "#367 talents scope")
endforeach()

# Core hooks only act with the bot auras.
file(READ "${TW_CORE_ROOT}/src/game/Objects/Object.cpp" object)
require_text("${object}" "DonePercent *= GetFunserverRogueTalentDamageMultiplier(pUnit, pVictim, spellProto);" "#367 damage hook")
require_text("${object}" "pVictim->HasAura(ROGUE_TALENT_GHOSTLY_EVASION)" "#367 magic dodge needs the bot aura")
file(READ "${TW_CORE_ROOT}/src/game/Spells/Spell.cpp" spell)
require_text("${spell}" "!IsFunserverFrontalBackstab(m_casterUnit, target, m_spellInfo)" "#367 frontal Backstab hook")
require_text("${spell}" "caster->GetAura(ROGUE_TALENT_FRONTAL_BACKSTAB, EFFECT_INDEX_0)" "#367 frontal Backstab needs the bot aura")
file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_rogue.cpp" rogue)
foreach (script "spell_rogue_cooldown_flow" "spell_rogue_vigor_energy" "spell_rogue_seal_fate_echo" "spell_rogue_riposte_flow"
                "spell_rogue_arcane_evasion" "spell_rogue_hemorrhage_stacks" "spell_rogue_shadow_edge")
  require_text("${rogue}" "(\"${script}\"" "#367 script registration")
endforeach()
message(STATUS "ROGUE_TALENTS_367_CONTRACT=PASS")
