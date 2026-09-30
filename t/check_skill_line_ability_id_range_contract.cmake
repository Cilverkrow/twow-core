if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#367: skill_line_ability.id is smallint unsigned. A larger id is truncated to 65535
# and INSERT IGNORE hides it (20260929100000 lost seven of eight rows that way).
# 1. No migration inserts a skill_line_ability id above 65535, except 20260929100000, which
#    is in the ledger and repaired by 20260930010000.
# 2. No migration inserts into skill_line_ability with INSERT IGNORE (same exception).
# 3. The follow-up carries every #219 row with id = spell - 60000 and otherwise equal values.
set(max_id 65535)
set(known_bad "20260929100000_world.sql")
set(followup "${TW_CORE_ROOT}/sql/database_updates/20260930010000_world.sql")

file(GLOB_RECURSE migrations "${TW_CORE_ROOT}/sql/database_updates/*.sql")
list(LENGTH migrations count)
if (count LESS 100)
  message(FATAL_ERROR "Only ${count} migrations found below ${TW_CORE_ROOT}/sql/database_updates")
endif()

set(checked 0)
foreach (path IN LISTS migrations)
  get_filename_component(name "${path}" NAME)
  file(READ "${path}" text)
  string(FIND "${text}" "skill_line_ability" at)
  if (at EQUAL -1 OR name STREQUAL known_bad)
    continue()
  endif()
  string(REGEX REPLACE "--[^\n]*" "" text "${text}")
  string(REGEX MATCHALL "(INSERT|REPLACE)[^;]*INTO[ \t\r\n]+`?skill_line_ability`?[^;]*" statements "${text}")
  foreach (statement IN LISTS statements)
    if (statement MATCHES "^INSERT[ \t\r\n]+IGNORE")
      message(FATAL_ERROR "${name}: INSERT IGNORE into skill_line_ability hides truncated ids")
    endif()
    string(REGEX REPLACE "^.*VALUES" "" values "${statement}")
    string(REGEX MATCHALL "\\([ \t\r\n]*[0-9]+" ids "${values}")
    foreach (id IN LISTS ids)
      string(REGEX REPLACE "[^0-9]" "" id "${id}")
      if (id GREATER max_id)
        message(FATAL_ERROR "${name}: skill_line_ability id ${id} exceeds smallint unsigned (${max_id})")
      endif()
      math(EXPR checked "${checked} + 1")
    endforeach()
  endforeach()
endforeach()

# 3. Every #219 row, re-keyed.
file(READ "${TW_CORE_ROOT}/sql/database_updates/${known_bad}" original)
string(REGEX MATCHALL "skill_line_ability`[^;]*VALUES \\(([0-9]+), ([^)]*)\\)" rows "${original}")
list(LENGTH rows row_count)
if (NOT row_count EQUAL 8)
  message(FATAL_ERROR "Expected 8 skill_line_ability rows in ${known_bad}, found ${row_count}")
endif()
file(READ "${followup}" fix)
foreach (row IN LISTS rows)
  string(REGEX MATCH "VALUES \\(([0-9]+), ([^)]*)\\)" _ "${row}")
  set(spell "${CMAKE_MATCH_1}")
  set(rest "${CMAKE_MATCH_2}")
  math(EXPR new_id "${spell} - 60000")
  string(FIND "${fix}" "(${new_id}, ${rest})" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Follow-up is missing the re-keyed row (${new_id}, ${rest})")
  endif()
endforeach()
foreach (required
    "DELETE FROM `skill_line_ability` WHERE `id` = 65535 AND `spell_id` = 90208;"
    "INSERT INTO `skill_line_ability`")
  string(FIND "${fix}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Follow-up is missing: ${required}")
  endif()
endforeach()

message(STATUS "SKILL_LINE_ABILITY_ID_RANGE_CONTRACT=PASS ids_checked=${checked}")
