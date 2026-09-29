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

# twow-repo#367 / #409 stage 2: server counterpart of the rogue client delta
# (twow-repo ops/clientpatch/changes/*/0367_*). Texts and icons only for the
# existing spells, the player poison ranks I-IV, and the trainer rows of P-1/P-2.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260929100000_world.sql" m)
# CMake lists split on ';' (statement ends, and the tooltip token $lpoint:points;).
string(REPLACE ";" "<SC>" m_nosc "${m}")

# 90140-90146 (kit) and 90150-90193 (talents, helpers): one text/icon UPDATE each.
string(REGEX MATCHALL "UPDATE `spell_template` SET [^\n]*" updates "${m_nosc}")
list(LENGTH updates update_count)
if (NOT update_count EQUAL 51)
  message(FATAL_ERROR "#367 client: expected 51 spell_template text updates, got ${update_count}")
endif()
foreach (u IN LISTS updates)
  if (NOT u MATCHES "^UPDATE `spell_template` SET `name` = '[^']*', `nameSubtext` = '[^']*', `description` = '.*', `auraDescription` = '.*', `spellIconId` = [0-9]+ WHERE `entry` = 901[4-9][0-9]<SC>$")
    message(FATAL_ERROR "#367 client: an UPDATE touches more than text and icon: ${u}")
  endif()
endforeach()
foreach (id RANGE 90140 90146)
  require_text("${m}" "WHERE `entry` = ${id};" "#367 kit text")
endforeach()
foreach (id RANGE 90150 90193)
  require_text("${m}" "WHERE `entry` = ${id};" "#367 talent text")
endforeach()

# Poison ranks I-IV: owner values (#386 D-1/D-7/D-8), own enchantment per rank.
foreach (required
    "`entry` = 90200, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 1', `spellLevel` = 20, `baseLevel` = 20, `effectBasePoints1` = 24, `effectDieSides1` = 8, `effectBasePoints2` = 149"
    "`entry` = 90201, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 2', `spellLevel` = 30, `baseLevel` = 30, `effectBasePoints1` = 35, `effectDieSides1` = 10, `effectBasePoints2` = 209"
    "`entry` = 90202, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 3', `spellLevel` = 40, `baseLevel` = 40, `effectBasePoints1` = 46, `effectDieSides1` = 13, `effectBasePoints2` = 274"
    "`entry` = 90203, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 4', `spellLevel` = 50, `baseLevel` = 50, `effectBasePoints1` = 56, `effectDieSides1` = 16, `effectBasePoints2` = 334"
    "`effectMiscValue1` = 90141, `spellIconId` = 110;"
    "`effectMiscValue1` = 90144, `spellIconId` = 110;"
    "`entry` = 90141, `name` = 'Agitating Poison I', `spellid_1` = 90204, `item_level` = 20, `required_level` = 20"
    "`entry` = 90144, `name` = 'Agitating Poison IV', `spellid_1` = 90207, `item_level` = 50, `required_level` = 50")
  require_text("${m}" "${required}" "#367 poison ranks")
endforeach()
string(REGEX MATCHALL "INSERT IGNORE INTO `spell_template` SELECT" spell_clones "${m}")
list(LENGTH spell_clones spell_clone_count)
string(REGEX MATCHALL "INSERT IGNORE INTO `item_template` SELECT" item_clones "${m}")
list(LENGTH item_clones item_clone_count)
# 4 procs + 4 coatings + 4 recipes + 4 recipe trainer spells + 4 kit trainer spells.
if (NOT spell_clone_count EQUAL 20 OR NOT item_clone_count EQUAL 4)
  message(FATAL_ERROR "#367: expected 20 spell and 4 item clones, got ${spell_clone_count} / ${item_clone_count}")
endif()

# Owner decisions P-1/P-2 (2026-09-28): the trainer teaches the kit and the recipes, only
# where it already teaches Agitating Poison (the rogue trainers), with the owner reagents.
foreach (required
    "`entry` = 90208, `nameSubtext` = 'Rank 1', `spellLevel` = 20, `effectItemType1` = 90141, `reagent1` = 2931, `reagentCount1` = 1, `reagent2` = 3372, `reagentCount2` = 1"
    "`entry` = 90211, `nameSubtext` = 'Rank 4', `spellLevel` = 50, `effectItemType1` = 90144, `reagent1` = 2931, `reagentCount1` = 2, `reagent2` = 3372, `reagentCount2` = 1"
    "`entry` = 90212, `nameSubtext` = 'Rank 1', `effectTriggerSpell1` = 90208;"
    "`entry` = 90216, `name` = 'Spit', `nameSubtext` = '', `description` = '', `effectTriggerSpell1` = 90140,"
    "`entry` = 90219, `name` = 'Shadow Dance', `nameSubtext` = 'Rank 3', `description` = '', `effectTriggerSpell1` = 90144,"
    "VALUES (90142, 38, 90142, 0, 8, 1, 90143, 0, 0, 0, 0);"
    "VALUES (90143, 38, 90143, 0, 8, 1, 90144, 0, 0, 0, 0);"
    "VALUES (90208, 40, 90208, 0, 8, 1, 0, 0, 175, 125, 0);"
    "SELECT `entry`, 90212, 2700, 40, 1, 20 FROM `npc_trainer` WHERE `spell` = 47312;"
    "SELECT `entry`, 90216, 720, 0, 0, 12 FROM `npc_trainer` WHERE `spell` = 47312;"
    "SELECT `entry`, 90219, 48600, 0, 0, 60 FROM `npc_trainer` WHERE `spell` = 47312;")
  require_text("${m}" "${required}" "#367 trainer and recipes")
endforeach()
string(REGEX MATCHALL "INSERT IGNORE INTO `npc_trainer`" trainer_rows "${m}")
list(LENGTH trainer_rows trainer_count)
string(REGEX MATCHALL "INSERT IGNORE INTO `skill_line_ability`" sla_rows "${m}")
list(LENGTH sla_rows sla_count)
if (NOT trainer_count EQUAL 8 OR NOT sla_count EQUAL 8)
  message(FATAL_ERROR "#367: expected 8 trainer and 8 skill_line_ability inserts, got ${trainer_count} / ${sla_count}")
endif()
foreach (forbidden "DELETE " "REPLACE " "npc_vendor" "loot_template"
                   "UPDATE `spell_template` SET `effect" "`script_name` = 'spell_rogue")
  forbid_text("${m}" "${forbidden}" "#367 client scope")
endforeach()
message(STATUS "ROGUE_CLIENT_367_CONTRACT=PASS")
