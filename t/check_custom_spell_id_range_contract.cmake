if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#455 (train 8b): the 1.12 client and parts of the protocol carry spell IDs in 16
# bits, so a custom spell >= 65536 reaches players as another spell. Custom spells live in
# 61002-65535 since 20261001090000 (90001-90219 moved by -28999).
# 1. The renumbering migration is there.
# 2. The code that names custom spells uses no spell ID above 65535.
# 3. No later migration writes a spell ID above 65535 into the spell tables.
set(limit 65535)
set(renumber "20261001090000_world.sql")

file(READ "${TW_CORE_ROOT}/sql/database_updates/${renumber}" m)
string(FIND "${m}" "UPDATE `spell_template` SET `entry` = `entry` - 28999 WHERE `entry` BETWEEN 90001 AND 90219;" at)
if (at EQUAL -1)
  message(FATAL_ERROR "Missing the train 8b renumbering in ${renumber}")
endif()

function(reject_large text what)
  string(REGEX MATCHALL "[0-9]+" numbers "${text}")
  foreach (n IN LISTS numbers)
    if (n GREATER limit)
      message(FATAL_ERROR "${what}: spell ID ${n} is above ${limit} (16-bit client spell IDs, #455)")
    endif()
  endforeach()
endfunction()

# 2. Files that only name spells by ID (comments carry issue comment numbers).
foreach (rel
    "modules/mod-playerbots/src/playerbot/SpecAuraPolicy.h"
    "src/game/FunserverRogueTalents.h")
  file(READ "${TW_CORE_ROOT}/${rel}" text)
  string(REGEX REPLACE "//[^\n]*" "" text "${text}")
  reject_large("${text}" "${rel}")
endforeach()

# The spell scripts also hold flag masks and the bot poison item 90140: only their spell
# constants count.
foreach (script shaman rogue)
  string(TOUPPER "${script}" upper)
  file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_${script}.cpp" text)
  string(REGEX MATCHALL "SPELL_${upper}_[A-Z0-9_]+[ \t]*=[ \t]*[0-9]+" constants "${text}")
  reject_large("${constants}" "spell_${script}.cpp")
endforeach()

# The premade generator finds the new talents by their first rank spell.
file(READ "${TW_CORE_ROOT}/modules/mod-playerbots/tools/build_premade_specs.py" generator)
string(REGEX MATCH "AURA_FIRST_SPELL = \\{[^}]*\\}" first_spells "${generator}")
string(REGEX MATCHALL "\\): [0-9]+" first_ids "${first_spells}")
string(REGEX MATCH "EXTRA_REAL_TALENTS = \\{[^\n]*\n[^\n]*" extra "${generator}")
string(REGEX MATCHALL "\\{[0-9]+:" extra_ids "${extra}")
list(LENGTH first_ids first_count)
if (first_count LESS 20)
  message(FATAL_ERROR "AURA_FIRST_SPELL not found in build_premade_specs.py (${first_count} IDs)")
endif()
reject_large("${first_ids};${extra_ids}" "build_premade_specs.py")

# 3. Migrations after the renumbering.
file(GLOB migrations "${TW_CORE_ROOT}/sql/database_updates/*.sql")
set(checked 0)
foreach (path IN LISTS migrations)
  get_filename_component(name "${path}" NAME)
  if (NOT name STRGREATER renumber)
    continue()
  endif()
  file(READ "${path}" text)
  string(REGEX REPLACE "--[^\n]*" "" text "${text}")
  string(REGEX MATCHALL "`(entry|spell|spell_id|superseded_by_spell|effectTriggerSpell[123]|spellid_[1-5])` = [0-9]+" assigned "${text}")
  string(REGEX MATCHALL "INTO `?(spell_template|spell_proc_event)`?[^;]*VALUES[ \t\r\n]*\\([ \t\r\n]*[0-9]+" inserted "${text}")
  foreach (item IN LISTS assigned inserted)
    string(REGEX MATCH "[0-9]+$" n "${item}")
    if (item MATCHES "^`entry`" AND NOT text MATCHES "spell_template|spell_proc_event")
      continue()
    endif()
    if (n GREATER limit)
      message(FATAL_ERROR "${name}: spell ID ${n} above ${limit} in '${item}' (#455)")
    endif()
    math(EXPR checked "${checked} + 1")
  endforeach()
endforeach()

message(STATUS "CUSTOM_SPELL_ID_RANGE_CONTRACT=PASS later_migration_ids=${checked}")
