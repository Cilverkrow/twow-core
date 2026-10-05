# twow-repo#511 (train 9): Alterac event item spells 61002-61007 lose their donor leftovers.
# Text contract on the migration: backup first, old-value guards, the four fixes, end-state CHECK,
# no other table and no destructive statement. Paths only from TW_CORE_ROOT (core CI and the
# twow-repo layout with the core under /src/core).
if (NOT TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "${label}: missing ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "${label}: must not contain ${needle}")
  endif()
endfunction()

set(name "20261004080000_world.sql")
# HEX read: a plain file(READ) on Windows hosts drops CR bytes. Byte-aligned search for 0d.
file(READ "${TW_CORE_ROOT}/sql/database_updates/${name}" raw_hex HEX)
if (raw_hex MATCHES "^(..)*0d")
  message(FATAL_ERROR "${name}: must use LF line endings")
endif()
file(READ "${TW_CORE_ROOT}/sql/database_updates/${name}" raw)

foreach (needle
    "twow-repo#511 train 9"
    "issuecomment-5977848252"
    "Coupling: client patch 8"
    "Rollback (exact):"
    "--   UPDATE `spell_template` s JOIN `spell_template_bak_511` b ON b.`entry` = s.`entry`")
  require_text("${raw}" "${needle}" "${name} header")
endforeach()

string(REGEX REPLACE "--[^\n]*" "" m "${raw}")
foreach (forbidden "DELETE " "REPLACE " "DROP TABLE" "TRUNCATE" "INSERT INTO `spell_template` " "item_template")
  forbid_text("${m}" "${forbidden}" "${name} scope")
endforeach()

# Backup before the first UPDATE.
require_text("${m}" "CREATE TABLE IF NOT EXISTS `spell_template_bak_511` LIKE `spell_template`;" "${name} backup")
string(FIND "${m}" "INSERT IGNORE INTO `spell_template_bak_511`" backup_at)
string(FIND "${m}" "UPDATE `spell_template`" first_update)
if (backup_at EQUAL -1 OR first_update LESS backup_at)
  message(FATAL_ERROR "${name}: backup must come before the first UPDATE")
endif()

# Exactly four UPDATEs, each guarded by entry and old values.
string(REPLACE ";" "\n<END>\n" stmts "${m}")
string(REGEX MATCHALL "UPDATE `spell_template`[^<]*<END>" updates "${stmts}")
list(LENGTH updates update_count)
if (NOT update_count EQUAL 4)
  message(FATAL_ERROR "${name}: expected 4 UPDATEs, got ${update_count}")
endif()
foreach (u IN LISTS updates)
  if (NOT u MATCHES "WHERE `entry` = 6100[2467] AND ")
    message(FATAL_ERROR "${name}: UPDATE without entry + old-value guard: ${u}")
  endif()
endforeach()

foreach (needle
    "WHERE `entry` = 61002 AND `effect3` = 6 AND `effectApplyAuraName3` = 65 AND `effectBasePoints3` = 5"
    "WHERE `entry` = 61007 AND `effect3` = 6 AND `effectApplyAuraName3` = 65 AND `effectBasePoints3` = 5"
    "SET `effect3` = 0, `effectApplyAuraName3` = 0, `effectBasePoints3` = 0, `effectDieSides3` = 0,"
    "`effectBaseDice3` = 0, `effectImplicitTargetA3` = 0, `effectMultipleValue3` = 0, `spellVisual1` = 0,"
    "'Damage taken reduced by 10%. Movement speed reduced by 80%.'"
    "'Attack and casting speed increased by $s1%.'"
    "SET `procCharges` = 0, `spellFamilyName` = 0,"
    "WHERE `entry` = 61004 AND `procCharges` = 1 AND `spellFamilyName` = 6 AND `effectTriggerSpell1` = 61005;"
    "WHERE `entry` = 61006 AND `procCharges` = 1 AND `spellFamilyName` = 6 AND `effectTriggerSpell1` = 61007;"
    "'Your attacks heal you for $61005s1 health.'"
    "'Your healing spells have a chance to imbue the target with a fiery temper.'")
  require_text("${m}" "${needle}" "#511 fix")
endforeach()

# End-state check after every write.
require_text("${m}" "CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_511` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));" "#511 end-state check")
string(FIND "${m}" "INSERT INTO `tmp_check_511` (`ok`)" check_at)
string(FIND "${m}" "UPDATE `spell_template`" last_update REVERSE)
if (check_at EQUAL -1 OR check_at LESS last_update)
  message(FATAL_ERROR "#511 end-state check must come last")
endif()
require_text("${m}" "(SELECT COUNT(*) FROM `spell_template_bak_511`) = 4;" "#511 end-state backup count")

message(STATUS "AV_ITEM_SPELLS_511_CONTRACT=PASS updates=${update_count}")
