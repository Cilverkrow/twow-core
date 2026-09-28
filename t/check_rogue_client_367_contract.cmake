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
# existing spells, and the player poison ranks I-IV, never trained or sold yet.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260928170000_world.sql" m)
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
if (NOT spell_clone_count EQUAL 8 OR NOT item_clone_count EQUAL 4)
  message(FATAL_ERROR "#367 poison ranks: expected 8 spell and 4 item clones")
endif()
foreach (forbidden "DELETE " "REPLACE " "npc_trainer" "npc_vendor" "skill_line_ability" "loot_template"
                   "UPDATE `spell_template` SET `effect" "`script_name` = 'spell_rogue")
  forbid_text("${m}" "${forbidden}" "#367 client scope")
endforeach()
message(STATUS "ROGUE_CLIENT_367_CONTRACT=PASS")
