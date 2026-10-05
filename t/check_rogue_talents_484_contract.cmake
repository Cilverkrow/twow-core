if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#484 train 9, rogue part (owner tests after 8.10, issuecomment-5972156860):
# Deep Wounds stacks, Shadow Dance passive in the data, trainer teaching spells visual 107,
# Brazen Strike (Backstab without the client "behind" bit, server keeps customFlags 0x40),
# Riposte Flow as named strikes 61221/61222 with double threat via spell_threat.
# Checks the migration text (decisions, old-value guards, backup, end-state CHECK), the
# script and the headers. The DB result itself was dry-run in a disposable database.

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

set(name "20261003200000_world.sql")
file(READ "${TW_CORE_ROOT}/sql/database_updates/${name}" raw)
string(FIND "${raw}" "\r" cr_at)
if (NOT cr_at EQUAL -1)
  message(FATAL_ERROR "${name}: must use LF line endings")
endif()

# Header: reason, owner decision, coupling, exact rollback.
foreach (needle
    "twow-repo#484 train 9"
    "issuecomment-5972156860"
    "Coupling: client patch 8"
    "Rollback (exact, in this order):"
    "--   UPDATE `spell_template` s JOIN `spell_template_bak_484_rogue` b ON b.`entry` = s.`entry`"
    "--   UPDATE `spell_extra` e JOIN `spell_extra_bak_484_rogue` b ON b.`entry` = e.`entry` SET e.`customFlags` = b.`customFlags`;"
    "--   DELETE FROM `spell_threat` WHERE `entry` IN (61221, 61222);"
    "--   DELETE FROM `spell_template` WHERE `entry` IN (61221, 61222);")
  require_text("${raw}" "${needle}" "${name} header")
endforeach()

# Everything below works on the statements only.
string(REGEX REPLACE "--[^\n]*" "" m "${raw}")
foreach (forbidden "DELETE " "REPLACE " "DROP TABLE" "TRUNCATE" "npc_trainer" "skill_line_ability" "SET `procFlags`")
  forbid_text("${m}" "${forbidden}" "${name} scope")
endforeach()

# Backups first, with old-value guards.
foreach (needle
    "CREATE TABLE IF NOT EXISTS `spell_template_bak_484_rogue` LIKE `spell_template`;"
    "INSERT IGNORE INTO `spell_template_bak_484_rogue`"
    "WHERE (`entry` = 61194 AND `name` = 'Hemorrhage' AND `stackAmount` = 4 AND `spellVisual1` = 5119)"
    "OR (`entry` IN (17347, 17348) AND `script_name` = '')"
    "OR (`entry` BETWEEN 61143 AND 61145 AND `attributes` = 327680)"
    "OR (`entry` BETWEEN 61213 AND 61220 AND `effect1` = 36 AND `spellVisual1` = 222 AND `interruptFlags` = 15)"
    "OR (`entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300) AND `attributesEx2` = 1048576)"
    "OR (`entry` = 61146 AND `spellFamilyName` = 8 AND `effectApplyAuraName1` = 49);"
    "CREATE TABLE IF NOT EXISTS `spell_extra_bak_484_rogue` LIKE `spell_extra`;"
    "INSERT IGNORE INTO `spell_extra_bak_484_rogue`")
  require_text("${m}" "${needle}" "${name} backup")
endforeach()
string(FIND "${m}" "INSERT IGNORE INTO `spell_extra_bak_484_rogue`" backup_at)
string(FIND "${m}" "UPDATE `spell_template`" first_update)
string(FIND "${m}" "UPDATE `spell_extra`" extra_update)
if (first_update LESS backup_at OR extra_update LESS backup_at)
  message(FATAL_ERROR "${name}: backups must come before the first UPDATE")
endif()

# Every UPDATE of a real table carries an old-value guard (entry plus at least one more condition).
string(REPLACE ";" "\n<END>\n" stmts "${m}")
string(REGEX MATCHALL "UPDATE `(spell_template|spell_extra)`[^<]*<END>" updates "${stmts}")
list(LENGTH updates update_count)
if (NOT update_count EQUAL 7)
  message(FATAL_ERROR "${name}: expected 7 guarded UPDATEs of spell_template/spell_extra, got ${update_count}")
endif()
foreach (u IN LISTS updates)
  if (NOT u MATCHES "WHERE[^<]*`entry`[^<]* AND ")
    message(FATAL_ERROR "${name}: UPDATE without old-value guard: ${u}")
  endif()
endforeach()

# 1. Deep Wounds 61194 (+ script on the deprecated Hemorrhage ranks).
foreach (needle
    "SET `name` = 'Deep Wounds', `nameSubtext` = '',"
    "`description` = 'Physical damage taken increased by $s1% per stack.',"
    "`auraDescription` = 'Physical damage taken increased by $s1% per stack.',"
    "`stackAmount` = 5, `spellVisual1` = 0, `powerType` = 0, `manaCost` = 0,"
    "`startRecoveryCategory` = 0, `startRecoveryTime` = 0"
    " WHERE `entry` = 61194 AND `name` = 'Hemorrhage' AND `stackAmount` = 4 AND `spellVisual1` = 5119"
    "AND `powerType` = 3 AND `manaCost` = 40 AND `startRecoveryCategory` = 133 AND `startRecoveryTime` = 1000;"
    "UPDATE `spell_template` SET `script_name` = 'spell_rogue_hemorrhage_stacks'\n WHERE `entry` IN (17347, 17348) AND `script_name` = '';")
  require_text("${m}" "${needle}" "#484 Deep Wounds")
endforeach()
foreach (forbidden "SET `spellIconId` =" ", `spellIconId` =")
  forbid_text("${m}" "${forbidden}" "#484 Deep Wounds keeps icon 153 (and the clones keep Riposte's)")
endforeach()

# 2. Shadow Dance passive in the data.
require_text("${m}" "UPDATE `spell_template` SET `attributes` = `attributes` | 64\n WHERE `entry` BETWEEN 61143 AND 61145 AND `attributes` = 327680;" "#484 Shadow Dance PASSIVE")

# 3. Trainer teaching spells: visual and interrupt flags only.
require_text("${m}" "UPDATE `spell_template` SET `spellVisual1` = 107, `interruptFlags` = 0\n WHERE `entry` BETWEEN 61213 AND 61220 AND `effect1` = 36 AND `spellVisual1` = 222 AND `interruptFlags` = 15;" "#484 trainer teaching spells")

# 4. Brazen Strike: both halves in one statement, never only one of them.
foreach (needle
    "UPDATE `spell_template` SET `attributesEx2` = `attributesEx2` & ~1048576, `customFlags` = `customFlags` | 64\n WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300)\n   AND `spellFamilyName` = 8 AND `attributesEx2` = 1048576 AND (`customFlags` & 64) = 0;"
    "UPDATE `spell_extra` SET `customFlags` = `customFlags` | 64\n WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300) AND (`customFlags` & 64) = 0;")
  require_text("${m}" "${needle}" "#484 Brazen Strike")
endforeach()

# 5. Riposte Flow strikes: tmp_spell clones of 14251 (talentdelta --core pattern), no disarm,
# aura state, cost, cooldown, category or Riposte family flags; OH bit only on 61222.
string(REGEX MATCHALL "INSERT IGNORE INTO `tmp_spell` SELECT [*] FROM `spell_template`\n WHERE `entry` = 14251 AND `name` = 'Riposte' AND `effect1` = 31 AND `dmgClass` = 2" donors "${m}")
list(LENGTH donors donor_count)
string(REGEX MATCHALL "INSERT IGNORE INTO `spell_template` SELECT [*] FROM `tmp_spell`" clones "${m}")
list(LENGTH clones clone_count)
if (NOT donor_count EQUAL 2 OR NOT clone_count EQUAL 2)
  message(FATAL_ERROR "#484 Riposte Flow: expected 2 guarded clones of 14251 (donors ${donor_count}, clones ${clone_count})")
endif()
foreach (id 61221 61222)
  string(FIND "${m}" "UPDATE `tmp_spell` SET `entry` = ${id}," at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#484 Riposte Flow: missing clone SET `entry` = ${id},")
  endif()
  string(SUBSTRING "${m}" ${at} 900 clone)
  string(FIND "${clone}" ";" clone_end)
  string(SUBSTRING "${clone}" 0 ${clone_end} clone)
  foreach (needle
      "`name` = 'Riposte Flow', `nameSubtext` = ''"
      "`auraDescription` = '', `category` = 0, `casterAuraState` = 0, `recoveryTime` = 0, `categoryRecoveryTime` = 0"
      "`startRecoveryCategory` = 0, `startRecoveryTime` = 0, `durationIndex` = 0, `powerType` = 0, `manaCost` = 0"
      "`attributesEx4` = 0, `spellFamilyFlags` = 0, `script_name` = ''"
      "`effectBasePoints1` = 99"
      "`effect2` = 0, `effectApplyAuraName2` = 0, `effectMechanic2` = 0, `effectImplicitTargetA2` = 0")
    require_text("${clone}" "${needle}" "#484 Riposte Flow ${id}")
  endforeach()
  forbid_text("${clone}" "`dmgClass`" "#484 Riposte Flow ${id} keeps the melee damage class")
  forbid_text("${clone}" "`effect1`" "#484 Riposte Flow ${id} keeps effect 31")
endforeach()
require_text("${m}" "deals $s1% main-hand weapon damage and causes double threat.',\n       `auraDescription` = '', `category` = 0" "#484 Riposte Flow MH text")
require_text("${m}" "deals $s1% off-hand weapon damage and causes double threat.',\n       `auraDescription` = '', `category` = 0" "#484 Riposte Flow OH text")
string(FIND "${m}" "SET `entry` = 61221," mh_at)
string(FIND "${m}" "SET `entry` = 61222," oh_at)
string(FIND "${m}" "`attributesEx3` = 512, `attributesEx4` = 0" mh_bits)
string(FIND "${m}" "`attributesEx3` = 16777728, `attributesEx4` = 0" oh_bits)
if (mh_bits LESS mh_at OR mh_bits GREATER oh_at OR oh_bits LESS oh_at)
  message(FATAL_ERROR "#484 Riposte Flow: REQUIRES_OFFHAND_WEAPON (16777216) only on 61222, NOT_A_PROC (512) on both")
endif()
require_text("${m}" "INSERT IGNORE INTO `spell_threat` (`entry`, `Threat`, `multiplier`, `ap_bonus`) VALUES\n  (61221, 0, 2, 0),\n  (61222, 0, 2, 0);" "#484 Riposte Flow double threat")

# 6. Shadow Dance dodge buff 61146 leaves family 8 (generic no-stack with Evasion, audit N1).
require_text("${m}" "UPDATE `spell_template` SET `spellFamilyName` = 0
 WHERE `entry` = 61146 AND `spellFamilyName` = 8 AND `effectApplyAuraName1` = 49;" "#484 Shadow Dance dodge buff family")
require_text("${raw}" "--          s.`spellFamilyName` = b.`spellFamilyName`;" "${name} rollback (family)")

# End state, after every write.
require_text("${m}" "CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_484_rogue` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));" "#484 end-state check")
string(FIND "${m}" "INSERT INTO `tmp_check_484_rogue` (`ok`)" check_at)
string(FIND "${m}" "INSERT IGNORE INTO `spell_threat`" threat_at)
if (check_at EQUAL -1 OR check_at LESS threat_at)
  message(FATAL_ERROR "#484 end-state check must come last")
endif()
string(SUBSTRING "${m}" ${check_at} -1 check)
foreach (needle
    "`entry` = 61194 AND `name` = 'Deep Wounds' AND `nameSubtext` = '' AND `stackAmount` = 5"
    "`entry` IN (16511, 17347, 17348) AND `script_name` = 'spell_rogue_hemorrhage_stacks') = 3"
    "`entry` BETWEEN 61143 AND 61145 AND `attributes` = 327744) = 3"
    "`spellVisual1` = 107 AND `interruptFlags` = 0) = 8"
    "AND (`attributesEx2` & 1048576) = 0 AND (`customFlags` & 64) = 64) = 9"
    "FROM `spell_extra`"
    "(61221, 512, "
    "(61222, 16777728, "
    "`spellFamilyFlags` = 0 AND `script_name` = '') = 2"
    "`Threat` = 0 AND `multiplier` = 2 AND `ap_bonus` = 0) = 2"
    "`script_name` = 'spell_rogue_riposte_flow' AND `procFlags` = 680) = 3"
    "`entry` = 61146 AND `spellFamilyName` = 0 AND `effectApplyAuraName1` = 49) = 1"
    "FROM `spell_template_bak_484_rogue`) = 24"
    "FROM `spell_extra_bak_484_rogue`) =")
  require_text("${check}" "${needle}" "#484 end-state check")
endforeach()

# Script: the named strike instead of a white swing, reach/facing first, 1 s lock, trace.
file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_rogue.cpp" rogue)
string(FIND "${rogue}" "struct spell_rogue_riposte_flow : public AuraScript" rf_at)
if (rf_at EQUAL -1)
  message(FATAL_ERROR "spell_rogue.cpp: spell_rogue_riposte_flow missing")
endif()
string(SUBSTRING "${rogue}" ${rf_at} -1 rf)
string(FIND "${rf}" "\n};" rf_end)
string(SUBSTRING "${rf}" 0 ${rf_end} rf)
foreach (forbidden "AttackerStateUpdate" "OnThreatCalculate" "m_extraAttack")
  forbid_text("${rf}" "${forbidden}" "Riposte Flow script (train 9: named strike, threat via spell_threat)")
endforeach()
foreach (needle
    "if (procSpell && (procSpell->Id == ROGUE_TALENT_RIPOSTE_FLOW_MAIN_HAND || procSpell->Id == ROGUE_TALENT_RIPOSTE_FLOW_OFF_HAND))"
    "strike = ROGUE_TALENT_RIPOSTE_FLOW_OFF_HAND;"
    "strike = ROGUE_TALENT_RIPOSTE_FLOW_MAIN_HAND;"
    "if (!owner->CanReachWithMeleeAutoAttack(victim))"
    "if (!owner->HasInArc(victim))"
    "owner->AddSpellCooldown(aura->GetId(), 0, time(nullptr) + 1);"
    "SpellCastResult const result = owner->CastSpell(victim, strike, true, nullptr, aura);"
    "owner->RemoveSpellCooldown(aura->GetId());"
    "TraceRogueTalent(owner, \"riposte_flow\", result == SPELL_CAST_OK, uint32(result), strike);")
  require_text("${rf}" "${needle}" "Riposte Flow script")
endforeach()
string(FIND "${rf}" "CanReachWithMeleeAutoAttack" reach_at)
string(FIND "${rf}" "AddSpellCooldown" cd_at)
string(FIND "${rf}" "CastSpell(victim" cast_at)
if (cd_at LESS reach_at OR cast_at LESS cd_at)
  message(FATAL_ERROR "Riposte Flow script: reach/facing check, then cooldown, then cast")
endif()
require_text("${rogue}" "RegisterAuraScript(\"spell_rogue_riposte_flow\", &GetAuraScript<spell_rogue_riposte_flow>);" "Riposte Flow registration")

file(READ "${TW_CORE_ROOT}/src/game/FunserverRogueTalents.h" talents)
foreach (needle
    "ROGUE_TALENT_RIPOSTE_FLOW_MAIN_HAND = 61221,"
    "ROGUE_TALENT_RIPOSTE_FLOW_OFF_HAND  = 61222,"
    "uint32_t constexpr FUNSERVER_TRACE_FIRST_LINE_EVENTS = 5;"
    "events >= FUNSERVER_TRACE_FIRST_LINE_EVENTS")
  require_text("${talents}" "${needle}" "FunserverRogueTalents.h")
endforeach()
forbid_text("${talents}" "events >= 20" "FunserverRogueTalents.h (first trace line after 5 events)")

# Passive list stays as a safeguard; the custom range reaches the 16-bit limit.
file(READ "${TW_CORE_ROOT}/src/game/FunserverPassiveSpells.h" passive)
foreach (needle
    "The list stays as a safeguard"
    "constexpr uint32_t FUNSERVER_CUSTOM_SPELL_MAX = 65535;")
  require_text("${passive}" "${needle}" "FunserverPassiveSpells.h")
endforeach()
file(READ "${TW_CORE_ROOT}/t/check_passive_spells_contract.cmake" passive_contract)
forbid_text("${passive_contract}" "GREATER 61220" "passive_spells_contract (hard 61220 bound)")

message(STATUS "ROGUE_TALENTS_484_CONTRACT=PASS updates=${update_count}")
