if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#484 train 9, shaman part (owner tests 03.10.2026, issuecomment-5972156860):
#   Attack Speed without the +2 % spell haste leftover, Charged Stormstrike in 4 ranks,
#   Storm Wisdom consumed as a whole stack by the next cast (own text and icon), Elemental
#   Weapons 3/4/5 stacks, Ancestral Arms via spell_learn_spell (check_shaman_84_contract.cmake).

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#484 shaman: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#484 shaman: forbidden ${label}: ${needle}")
  endif()
endfunction()

# --- Migration -------------------------------------------------------------------------
set(migration_name "20261003200500_world.sql")
file(READ "${TW_CORE_ROOT}/sql/database_updates/${migration_name}" m)
string(FIND "${m}" "\r" cr)
if (NOT cr EQUAL -1)
  message(FATAL_ERROR "#484 shaman: ${migration_name} must use LF line endings")
endif()
# Statements only (comments carry the rollback SQL).
string(REGEX REPLACE "--[^\n]*" "" sql "${m}")

# Backup first, before any UPDATE.
string(FIND "${sql}" "CREATE TABLE IF NOT EXISTS `spell_template_bak_484_shaman` LIKE `spell_template`;" backup_at)
string(FIND "${sql}" "UPDATE `spell_template`" update_at)
if (backup_at EQUAL -1 OR update_at EQUAL -1 OR NOT backup_at LESS update_at)
  message(FATAL_ERROR "#484 shaman: the backup table must be created before the first UPDATE")
endif()
require_text("${sql}" "INSERT IGNORE INTO `spell_template_bak_484_shaman`" "backup rows")

foreach (required
    # Attack Speed: effect 3 (aura 65) removed, guarded by the old values.
    "WHERE `entry` BETWEEN 61101 AND 61105 AND `effect3` = 6 AND `effectApplyAuraName3` = 65"
    "SET `effect3` = 0, `effectApplyAuraName3` = 0, `effectBasePoints3` = 0"
    # Charged Stormstrike: rank 1 stays 61118, ranks 2-4 as tmp_spell clones (talentdelta --core).
    "SET `nameSubtext` = 'Rank 1',"
    "'Stormstrike consumes 1 Lightning Shield charge, increasing its damage by 10%.'"
    "SET `entry` = 61223, `nameSubtext` = 'Rank 2',"
    "SET `entry` = 61224, `nameSubtext` = 'Rank 3',"
    "SET `entry` = 61225, `nameSubtext` = 'Rank 4',"
    "'Stormstrike consumes up to 2 Lightning Shield charges, increasing its damage by 10% per charge.'"
    "'Stormstrike consumes up to 3 Lightning Shield charges, increasing its damage by 10% per charge.'"
    "'Stormstrike consumes up to 4 Lightning Shield charges, increasing its damage by 10% per charge.'"
    "INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;"
    # Storm Wisdom buffs: own text, talent icon.
    "`auraDescription` = 'Cast time and mana cost of your next Lightning Bolt reduced by 20% per stack.'"
    "`auraDescription` = 'Cast time and mana cost of your next Lightning Bolt or Chain Lightning reduced by 20% per stack.'"
    "`spellIconId` = 62"
    "WHERE `entry` = 61124 AND `spellIconId` = 212"
    "WHERE `entry` = 61126 AND `spellIconId` = 212"
    # Elemental Weapons / Rushing Winds: 3/4/5 stacks, rank texts.
    "SET `stackAmount` = 3 WHERE `entry` = 52967 AND `stackAmount` = 2;"
    "SET `stackAmount` = 5 WHERE `entry` = 52969 AND `stackAmount` = 6;"
    "'Stacks up to $52968u times.')"
    "'Stacks up to $52969u times.')"
    # Ancestral Arms.
    "(61131, 201, 1)"
    "(61131, 202, 1)"
    "(61131, 61132, 1)"
    # End state.
    "CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_484_shaman` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));"
    "INSERT INTO `tmp_check_484_shaman` (`ok`)")
  require_text("${sql}" "${required}" "${migration_name}")
endforeach()

# Exactly the three new spells, nothing deleted or replaced, no permanent table dropped.
string(REGEX MATCHALL "SET `entry` = [0-9]+," clones "${sql}")
list(LENGTH clones clone_count)
if (NOT clone_count EQUAL 3)
  message(FATAL_ERROR "#484 shaman: expected 3 spell clones (61223-61225), got ${clone_count}")
endif()
foreach (forbidden "DELETE " "TRUNCATE " "REPLACE INTO" "DROP TABLE")
  forbid_text("${sql}" "${forbidden}" "${migration_name} statement")
endforeach()
# Every UPDATE of spell_template carries an old-value guard (WHERE ... AND ...).
# (The match stops before the ';', which would split the CMake list.)
string(REGEX MATCHALL "UPDATE `spell_template`[^;]*" updates "${sql}")
list(LENGTH updates update_count)
if (NOT update_count EQUAL 9)
  message(FATAL_ERROR "#484 shaman: expected 9 UPDATEs of spell_template, got ${update_count}")
endif()
foreach (update IN LISTS updates)
  if (NOT update MATCHES "WHERE `entry`[^;]* AND ")
    message(FATAL_ERROR "#484 shaman: UPDATE without an old-value guard: ${update}")
  endif()
endforeach()
# The rollback in the header names every touched object.
foreach (required
    "-- Rollback (exact, in this order):"
    "JOIN `spell_template_bak_484_shaman` b ON b.`entry` = s.`entry`"
    "DELETE FROM `spell_template` WHERE `entry` IN (61223, 61224, 61225);"
    "DELETE FROM `spell_learn_spell` WHERE `entry` = 61131 AND `SpellID` IN (201, 202, 61132);"
    "UPDATE `skill_line_ability` s JOIN `bak_484_skill_line_ability` b ON b.`id` = s.`id` SET s.`class_mask` = b.`class_mask`;")
  require_text("${m}" "${required}" "${migration_name} rollback")
endforeach()

# Sword proficiency rows for shamans (#455 / OB-15): backup first, old-value guards, both rows, end state.
foreach (required
    "CREATE TABLE IF NOT EXISTS `bak_484_skill_line_ability` LIKE `skill_line_ability`;"
    "WHERE (`id` = 5 AND `spell_id` = 201 AND `class_mask` = 399)"
    "OR (`id` = 7 AND `spell_id` = 202 AND `class_mask` = 7);"
    "UPDATE `skill_line_ability` SET `class_mask` = 463 WHERE `id` = 5 AND `spell_id` = 201 AND `class_mask` = 399;"
    "UPDATE `skill_line_ability` SET `class_mask` = 71  WHERE `id` = 7 AND `spell_id` = 202 AND `class_mask` = 7;"
    "WHERE (`id`, `spell_id`, `class_mask`) IN ((5, 201, 463), (7, 202, 71))) = 2")
  require_text("${m}" "${required}" "${migration_name} shaman sword proficiency")
endforeach()
string(FIND "${m}" "INSERT IGNORE INTO `bak_484_skill_line_ability`" sla_backup_at)
string(FIND "${m}" "UPDATE `skill_line_ability` SET `class_mask` = 463" sla_update_at)
if (sla_backup_at EQUAL -1 OR sla_update_at LESS sla_backup_at)
  message(FATAL_ERROR "${migration_name}: skill_line_ability backup must come before its UPDATE")
endif()

# --- Storm Wisdom consume-on-use (core) --------------------------------------------------
file(READ "${TW_CORE_ROOT}/src/game/FunserverStackedSpellMods.h" stacked)
string(REGEX MATCH "FUNSERVER_CONSUME_ON_USE_STACK_MODS\\[\\] = \\{[^}]*\\}" list_text "${stacked}")
string(REGEX REPLACE "//[^\n]*" "" list_text "${list_text}")
string(REGEX MATCHALL "[0-9][0-9][0-9][0-9][0-9]+" stacked_ids "${list_text}")
if (NOT stacked_ids STREQUAL "61124;61126")
  message(FATAL_ERROR "#484 shaman: consume-on-use list must be exactly 61124, 61126 (got '${stacked_ids}')")
endif()
foreach (id IN LISTS stacked_ids)
  if (id LESS 61002 OR id GREATER 65535)
    message(FATAL_ERROR "#484 shaman: consume-on-use id ${id} outside our custom range 61002-65535")
  endif()
endforeach()
require_text("${stacked}" "inline bool IsFunserverConsumeOnUseStackMod(uint32_t spellId)" "lookup helper")

file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellAuras.cpp" auras)
require_text("${auras}" "#include \"FunserverStackedSpellMods.h\"" "SpellAuras.cpp include")
string(FIND "${auras}" "void Aura::HandleAddModifier(bool apply, bool Real)" add_mod_begin)
string(FIND "${auras}" "void Aura::TriggerSpell()" add_mod_end)
if (add_mod_begin EQUAL -1 OR add_mod_end EQUAL -1 OR NOT add_mod_begin LESS add_mod_end)
  message(FATAL_ERROR "#484 shaman: Aura::HandleAddModifier not found")
endif()
math(EXPR add_mod_length "${add_mod_end} - ${add_mod_begin}")
string(SUBSTRING "${auras}" ${add_mod_begin} ${add_mod_length} add_mod)
foreach (required
    "int16 charges = GetSpellProto()->StackAmount > 1 ? 0 : int16(GetHolder()->GetAuraCharges());"
    "if (IsFunserverConsumeOnUseStackMod(GetSpellProto()->Id))"
    "charges = 1;")
  require_text("${add_mod}" "${required}" "HandleAddModifier")
endforeach()

# --- Scripts -----------------------------------------------------------------------------
file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_shaman.cpp" script)
foreach (required
    "SPELL_SHAMAN_CHARGED_STORMSTRIKE_R1  = 61118,"
    "SPELL_SHAMAN_CHARGED_STORMSTRIKE_R2  = 61223,"
    "SPELL_SHAMAN_CHARGED_STORMSTRIKE_R3  = 61224,"
    "SPELL_SHAMAN_CHARGED_STORMSTRIKE_R4  = 61225,"
    "uint32 GetChargedStormstrikeRank(Unit* caster)"
    "return std::min(GetLightningShieldCharges(caster), rank);"
    "[ShamanTalentTrace] player=%u talent=%s"
    "TraceShamanTalent(owner, \"storm_wisdom\","
    "TraceShamanTalent(caster, \"charged_stormstrike\","
    "#include \"FunserverRogueTalents.h\"")
  require_text("${script}" "${required}" "spell_shaman.cpp")
endforeach()
forbid_text("${script}" "STORMSTRIKE_MAX_CONSUMED_CHARGES" "spell_shaman.cpp fixed 3-charge cap")
# Storm Wisdom: a crit while a Lightning Bolt in flight has consumed the stack starts a new buff.
require_text("${script}" "if (mod->GetSpellModifier() && mod->GetSpellModifier()->charges == -1)\n                    owner->RemoveAurasDueToSpell(buffId);" "spell_shaman.cpp Storm Wisdom consumed stack")
# Retaliation (audit N2): only melee-range attackers trigger the free Lightning Shield hit.
require_text("${script}" "if (!victim || !owner->CanReachWithMeleeAutoAttack(victim))\n            return SPELL_AURA_PROC_FAILED;" "spell_shaman.cpp Retaliation reach")

# --- Bots: SpecAura table and premade generator know the 4 ranks ---------------------------
file(READ "${TW_CORE_ROOT}/modules/mod-playerbots/src/playerbot/SpecAuraPolicy.h" policy)
require_text("${policy}" "{ \"stormstrike charges\", ShamanTank,  30, { 61118, 61223, 61224, 61225 } }," "SpecAuraPolicy.h ranks")
file(READ "${TW_CORE_ROOT}/modules/mod-playerbots/tools/build_premade_specs.py" generator)
require_text("${generator}" "(7, 'stormstrike charges', 30, 4, {'shaman tank'})," "build_premade_specs.py ranks")
require_text("${generator}" "(7, 'stormstrike charges'): 61118," "build_premade_specs.py first rank")

message(STATUS "SHAMAN_TALENTS_484_CONTRACT=PASS")
