if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#295 (main train 9, owner decisions 2026-10-02): the four world migrations for riding
# ranks (W1), mount items (W2a), mount spells (W2b) and player speed (W3).
# 1. Every file is there, LF only, names the issue and documents an exact Rollback.
# 2. Backups (<table>_bak_295) come before the first change, and every updated table is backed
#    up first in the same file.
# 3. No DELETE, DROP, TRUNCATE or REPLACE INTO outside comments.
# 4. New spells only in the reserved block 61300-61399, skill_line_ability rows only 30300/30301
#    (no INSERT IGNORE), and the rollback removes exactly the inserted keys.
# 5. Each file asserts its end state (CHECK tail), and W2a never mentions the spell table, so
#    its item entries cannot count as spell IDs (#455 range contract).
# 6. Every header says that the four files are rolled back together. W1 documents the
#    character step a rollback after go-live needs (riding capped at 150, backup first); it is
#    not part of the world Rollback block.
# 7. Round-2 review: the W2b conflict basepoint changes only rows backed up with the old value
#    99 (and the tail asserts those backups); the W2a rocket cars sell back for 0 like every
#    other racial family-1 mount (owner decision 2026-10-02).
set(dir "${TW_CORE_ROOT}/sql/database_updates")
set(w1 "20261002210000_world.sql")
set(w2a "20261002211000_world.sql")
set(w2b "20261002212000_world.sql")
set(w3 "20261002213000_world.sql")

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "${label}: missing ${needle}")
  endif()
endfunction()

set(new_spells "")
foreach (name ${w1} ${w2a} ${w2b} ${w3})
  if (NOT EXISTS "${dir}/${name}")
    message(FATAL_ERROR "Missing #295 migration ${name}")
  endif()
  # LF only (file(READ) drops CR on Windows hosts, so look at the bytes).
  file(READ "${dir}/${name}" hex HEX)
  string(REGEX MATCHALL "[0-9a-f][0-9a-f]" bytes "${hex}")
  list(FIND bytes "0d" cr)
  if (NOT cr EQUAL -1)
    message(FATAL_ERROR "${name}: CR line ending (migrations are LF)")
  endif()
  file(READ "${dir}/${name}" text)
  string(FIND "${text}" "-- twow-repo#295 " first)
  if (NOT first EQUAL 0)
    message(FATAL_ERROR "${name}: the header must start with the issue (-- twow-repo#295 ...)")
  endif()

  # Rollback: the header block after "-- Rollback", one SQL line per "--   " line.
  string(FIND "${text}" "\n-- Rollback" rb_at)
  if (rb_at EQUAL -1)
    message(FATAL_ERROR "${name}: no Rollback block in the header")
  endif()
  string(SUBSTRING "${text}" ${rb_at} -1 rb_tail)
  string(REGEX MATCH "^\n-- Rollback[^\n]*\n(--   [^\n]*\n)+" rollback "${rb_tail}")
  if (NOT rollback MATCHES "\n--   (UPDATE|DELETE) [^;]*;\n")
    message(FATAL_ERROR "${name}: the Rollback block holds no SQL statement")
  endif()
  if (rollback MATCHES "character_")
    message(FATAL_ERROR "${name}: the world Rollback block must not touch character tables")
  endif()
  string(SUBSTRING "${text}" 0 ${rb_at} header)
  if (NOT header MATCHES "\n-- W1, W2a, W2b and W3 [^\n]*rolled back together")
    message(FATAL_ERROR "${name}: the header must say that W1, W2a, W2b and W3 are rolled back together")
  endif()

  string(REGEX REPLACE "--[^\n]*" "" statements "${text}")
  string(TOUPPER "${statements}" upper)
  foreach (forbidden "DELETE " "DELETE\n" "DROP " "DROP\n" "TRUNCATE" "REPLACE INTO")
    string(FIND "${upper}" "${forbidden}" at)
    if (NOT at EQUAL -1)
      string(STRIP "${forbidden}" word)
      message(FATAL_ERROR "${name}: ${word} outside comments (forward-only, rollback lives in the header)")
    endif()
  endforeach()

  # Backups first: the first change (UPDATE or INSERT INTO a real table) comes after the first
  # backup, and every updated table has its own backup earlier in the file.
  string(REGEX MATCHALL "INSERT IGNORE INTO `[a-z_]+_bak_295`" backups "${statements}")
  list(LENGTH backups backup_count)
  if (backup_count EQUAL 0)
    message(FATAL_ERROR "${name}: no <table>_bak_295 backup")
  endif()
  string(FIND "${statements}" "INSERT IGNORE INTO `" first_backup)
  string(REGEX MATCHALL "(UPDATE|INSERT INTO) `[a-z_0-9]+`" changes "${statements}")
  foreach (change IN LISTS changes)
    string(REGEX MATCH "`([a-z_0-9]+)`" _ "${change}")
    set(table "${CMAKE_MATCH_1}")
    if (table MATCHES "_bak_295$" OR table MATCHES "^tmp_check_295_")
      continue()
    endif()
    string(FIND "${statements}" "${change}" change_at)
    if (NOT first_backup LESS change_at)
      message(FATAL_ERROR "${name}: '${change}' comes before the first backup")
    endif()
    if (change MATCHES "^UPDATE")
      string(FIND "${statements}" "INSERT IGNORE INTO `${table}_bak_295`" backup_at)
      if (backup_at EQUAL -1 OR NOT backup_at LESS change_at)
        message(FATAL_ERROR "${name}: ${table} is updated without a ${table}_bak_295 backup before it")
      endif()
    endif()
  endforeach()

  # End-state assertion (the updater ignores statement results).
  require_text("${statements}" "CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_295_" "${name} end-state check")
  require_text("${statements}" "CHECK (`ok` = 1)" "${name} end-state check")

  # New spell IDs: clones, chain ranks, trainer spells and learned spells.
  string(REGEX MATCHALL "INSERT INTO `spell_template` [(][^;]*[)]\nSELECT [0-9]+," clones "${statements}")
  string(REGEX MATCHALL "INSERT INTO `(spell_chain|spell_learn_spell|npc_trainer_template)` [(][^)]*[)]\nSELECT [0-9]+, [0-9]+," others "${statements}")
  foreach (insert IN LISTS clones others)
    string(REGEX MATCH "INTO `([a-z_]+)`" _ "${insert}")
    set(table "${CMAKE_MATCH_1}")
    string(REGEX MATCH "SELECT ([0-9]+),( ([0-9]+),)?$" _ "${insert}")
    set(id "${CMAKE_MATCH_1}")
    if (table MATCHES "^(spell_learn_spell|npc_trainer_template)$")
      set(id "${CMAKE_MATCH_3}")
    endif()
    if (id LESS 61300 OR id GREATER 61399)
      message(FATAL_ERROR "${name}: new spell ${id} outside the #295 block 61300-61399")
    endif()
    # The rollback deletes exactly this key from the same table.
    string(REGEX MATCHALL "DELETE FROM `${table}` WHERE [^;]*;" deletes "${rollback}")
    string(REGEX MATCH "[^0-9]${id}[^0-9]" in_rollback "${deletes}")
    if (in_rollback STREQUAL "")
      message(FATAL_ERROR "${name}: the rollback does not delete ${id} from ${table}")
    endif()
    list(APPEND new_spells ${id})
  endforeach()

  # skill_line_ability: only 30300/30301 (smallint unsigned, rule R6), never INSERT IGNORE.
  string(REGEX MATCHALL "INSERT[^;]*INTO `skill_line_ability`[^;]*" sla "${statements}")
  foreach (insert IN LISTS sla)
    if (insert MATCHES "^INSERT IGNORE")
      message(FATAL_ERROR "${name}: INSERT IGNORE into skill_line_ability")
    endif()
    if (NOT insert MATCHES "\nSELECT (30300|30301), d.`skill_id`, (61300|61302),")
      message(FATAL_ERROR "${name}: skill_line_ability row outside 30300/30301 or for another spell")
    endif()
    string(FIND "${rollback}" "DELETE FROM `skill_line_ability` WHERE `id` IN (30300, 30301);" sla_rollback)
    if (sla_rollback EQUAL -1)
      message(FATAL_ERROR "${name}: the rollback does not remove skill_line_ability 30300/30301")
    endif()
  endforeach()
endforeach()

list(REMOVE_DUPLICATES new_spells)
list(SORT new_spells)
if (NOT new_spells STREQUAL "61300;61301;61302;61303;61310")
  message(FATAL_ERROR "Expected the new spells 61300-61303 and 61310, found '${new_spells}'")
endif()

# W1: the trainer stages, the level-10 override and the riding ranks.
file(READ "${dir}/${w1}" text)
string(REGEX REPLACE "--[^\n]*" "" w1_statements "${text}")
foreach (required
    "SET `spellcost` = 5000, `reqlevel` = 10"
    "AND `spellcost` = 900000 AND `reqskill` = 0 AND `reqskillvalue` = 0 AND `reqlevel` = 40;"
    "SET `spellcost` = 50000, `reqlevel` = 20"
    "AND `spellcost` = 9000000 AND `reqskill` = 762 AND `reqskillvalue` = 0 AND `reqlevel` = 60;"
    "SELECT 1, 61301, 500000, 762, 150, 40 FROM DUAL"
    "SELECT 1, 61303, 5000000, 762, 225, 60 FROM DUAL"
    "SELECT 890, -1, -1, -1, -1, 10, -1, -1, 'twow-repo#295 riding from level 10' FROM DUAL"
    "SELECT 61300, 33391, 33388, 3, 0 FROM DUAL"
    "SELECT 61302, 61300, 33388, 4, 0 FROM DUAL")
  require_text("${w1_statements}" "${required}" "W1 riding ranks")
endforeach()
# After go-live the old core would unmount riding 225/300 (and drop 61300/61302/61310 at login),
# so a rollback caps riding at 150 in the character DB (OB-40), backup first.
foreach (required
    "\n--   CREATE TABLE IF NOT EXISTS `character_skills_bak_295` LIKE `character_skills`;\n"
    "\n--   INSERT IGNORE INTO `character_skills_bak_295` SELECT * FROM `character_skills` WHERE `skill` = 762;\n"
    "\n--   UPDATE `character_skills` SET `value` = LEAST(`value`, 150), `max` = LEAST(`max`, 150) WHERE `skill` = 762;\n")
  require_text("${text}" "${required}" "W1 character step for a rollback after go-live")
endforeach()

# W2a: item_template only.
file(READ "${dir}/${w2a}" text)
string(FIND "${text}" "spell_template" spell_table)
if (NOT spell_table EQUAL -1)
  message(FATAL_ERROR "${w2a} must not mention the spell table (item entries would count as spell IDs)")
endif()
string(REGEX REPLACE "--[^\n]*" "" w2a_statements "${text}")
string(REGEX MATCHALL "(UPDATE|INSERT INTO|INSERT IGNORE INTO) `[a-z_0-9]+`" w2a_tables "${w2a_statements}")
foreach (change IN LISTS w2a_tables)
  if (NOT change MATCHES "`(item_template|item_template_bak_295|tmp_check_295_w2a)`$")
    message(FATAL_ERROR "${w2a} changes another table: ${change}")
  endif()
endforeach()
# The rocket cars sell back for 0 like every other racial family-1 mount (owner 2026-10-02).
require_text("${w2a_statements}"
  "UPDATE `item_template` SET `sell_price` = 0\n WHERE `sell_price` = 20000 AND `buy_price` = 10000 AND `entry` IN (\n  80460, 80461, 80462\n );"
  "W2a rocket car sell price")
if (w2a_statements MATCHES "SET `sell_price` = [1-9]")
  message(FATAL_ERROR "${w2a}: a racial family-1 mount sells back for more than 0")
endif()

# W2b: the conflict basepoint 99 -> 59 only on mount spells with aura 32 at effect 2, and only
# on rows whose backup holds the old value 99, so the header rollback restores every change.
file(READ "${dir}/${w2b}" text)
string(REGEX REPLACE "--[^\n]*" "" w2b_statements "${text}")
require_text("${w2b_statements}"
  "UPDATE `spell_template` SET `effectBasePoints2` = 59\n WHERE `effectApplyAuraName1` = 78 AND `effectApplyAuraName2` = 32\n   AND `effectBasePoints2` = 99 AND `effectBaseDice2` = 1 AND `effectDieSides2` = 1\n   AND `entry` IN (SELECT b.`entry` FROM `spell_template_bak_295` b WHERE b.`effectBasePoints2` = 99)\n"
  "W2b conflict mount spells (tied to their backup)")
require_text("${w2b_statements}"
  "AND (SELECT COUNT(*) FROM `spell_template_bak_295` WHERE `effectBasePoints2` = 99 AND `entry` IN ("
  "W2b end-state check of the conflict backups")

# W3: the Blink passive is learned with Blink; Blink 1953 itself is not changed.
file(READ "${dir}/${w3}" text)
string(REGEX REPLACE "--[^\n]*" "" w3_statements "${text}")
require_text("${w3_statements}" "SELECT 1953, 61310, 1 FROM DUAL" "W3 Blink passive")
if (w3_statements MATCHES "UPDATE `spell_template`[^;]*[^0-9]1953[^0-9]")
  message(FATAL_ERROR "${w3}: Blink 1953 is shared with NPCs and must stay unchanged")
endif()

message(STATUS "RIDING_MIGRATIONS_295_CONTRACT=PASS spells=${new_spells}")
