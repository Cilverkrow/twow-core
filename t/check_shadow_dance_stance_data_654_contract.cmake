if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#654 (main train, Ae15): the spell rows of the Shadow Dance rogue tank stance (migration 20261029230000_world.sql).
# 61230 the stance: damage done -40 % / taken -25 % (all schools), unlimited, not passive, the stance script; 61231 / 61232 the energy
# helpers: -10 % energy regeneration for 3 s, icon 1 (no generic no-stack), not cancelable; the end-state CHECK.
set(mig "sql/database_updates/20261029230000_world.sql")
file(READ "${TW_CORE_ROOT}/${mig}" m)
string(REPLACE "\r" "" m "${m}")

function(require_text needle what)
  string(FIND "${m}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#654 stance data: missing ${what}: ${needle}")
  endif()
endfunction()

# 1. The stance 61230: clone of Righteous Fury like the ranks, the two damage effects, unlimited, the script, not made passive.
require_text("INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 25780;\nUPDATE `tmp_spell` SET `entry` = 61230," "61230 cloned from 25780")
require_text("`script_name` = 'spell_rogue_shadow_dance_stance'," "the stance script")
require_text("`effect1` = 6, `effectApplyAuraName1` = 79, `effectMiscValue1` = 127, `effectBasePoints1` = -41," "damage done -40 % (all schools)")
require_text("`effect2` = 6, `effectApplyAuraName2` = 87, `effectMiscValue2` = 127, `effectBasePoints2` = -26," "damage taken -25 % (all schools)")
require_text("`effectDieSides2` = 1, `effectBaseDice2` = 1, `effectImplicitTargetA2` = 1," "effect 2: one die, self target")
require_text("`durationIndex` = 21, `powerType` = 3, `manaCost` = 0," "unlimited, no cost")
string(FIND "${m}" "`attributes` = `attributes` | 64" passive)
if (NOT passive EQUAL -1)
  message(FATAL_ERROR "#654 stance data: the stance must not be made passive")
endif()

# 2. The helpers 61231 / 61232: -10 % energy for 3 s, icon 1, not cancelable.
foreach (id 61231 61232)
  require_text("UPDATE `tmp_spell` SET `entry` = ${id}, `spellFamilyName` = 0, `spellFamilyFlags` = 0, `script_name` = ''," "helper ${id}")
endforeach()
string(REGEX MATCHALL "`effect1` = 6, `effectApplyAuraName1` = 110, `effectMiscValue1` = 3, `effectBasePoints1` = -11," helpers "${m}")
list(LENGTH helpers helper_count)
if (NOT helper_count EQUAL 2)
  message(FATAL_ERROR "#654 stance data: expected two energy helpers (aura 110, energy, -10 %), found ${helper_count}")
endif()
string(REGEX MATCHALL "`attributes` = 2147483648,\n    `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `durationIndex` = 27, `spellIconId` = 1, `spellVisual1` = 0," flags "${m}")
list(LENGTH flags flag_count)
if (NOT flag_count EQUAL 2)
  message(FATAL_ERROR "#654 stance data: both helpers need CANT_CANCEL, 3 s and icon 1 (found ${flag_count})")
endif()

# 3. Replay-safe clones and the end-state CHECK.
string(REGEX MATCHALL "INSERT IGNORE INTO `spell_template` SELECT \\* FROM `tmp_spell`" inserts "${m}")
list(LENGTH inserts insert_count)
if (NOT insert_count EQUAL 3)
  message(FATAL_ERROR "#654 stance data: expected 3 INSERT IGNORE clones, found ${insert_count}")
endif()
require_text("CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_654_stance` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));" "the end-state CHECK")

message(STATUS "#654 stance data contract passed")
