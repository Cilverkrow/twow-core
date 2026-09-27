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

function(require_equal actual expected label)
  if (NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "${label} differ.\n  actual:   ${actual}\n  expected: ${expected}")
  endif()
endfunction()

# twow-repo#379: dwarf shaman (3,7) and undead paladin (5,2). The migration adds exactly
# these rows, only for these two pairs, replay-safe, and fails closed.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927120000_world.sql" m)
if (m MATCHES "\r")
  message(FATAL_ERROR "#379 migration must use LF line endings")
endif()

# --- scope: nothing but inserts into the five playercreateinfo/levelstats tables
foreach (forbidden "UPDATE " "DELETE " "REPLACE " "ALTER " "DROP TABLE" "TRUNCATE" "INSERT INTO `playercreateinfo`" "INSERT INTO `player_levelstats`" "INSERT INTO `playercreateinfo_spell`" "INSERT INTO `playercreateinfo_action`")
  forbid_text("${m}" "${forbidden}" "#379 scope")
endforeach()
string(REGEX MATCHALL "INSERT (IGNORE )?INTO `[a-z_0-9]+`" inserts "${m}")
set(expected_inserts
  "INSERT IGNORE INTO `player_levelstats`"
  "INSERT IGNORE INTO `playercreateinfo_spell`"
  "INSERT IGNORE INTO `playercreateinfo_action`"
  "INSERT INTO `playercreateinfo_item`"
  "INSERT INTO `playercreateinfo_item`"
  "INSERT IGNORE INTO `playercreateinfo`"
  "INSERT IGNORE INTO `playercreateinfo`"
  "INSERT INTO `_tw_379_assert`")
require_equal("${inserts}" "${expected_inserts}" "#379 insert statements (order: levelstats first, playercreateinfo last)")

# --- every VALUES tuple belongs to one of the two pairs
# No trailing , or ; in the match: a ; would split the CMake list element.
string(REGEX MATCHALL "\n\\([0-9]+, [0-9]+, [^\n]*\\)" tuples "${m}")
set(levelstats "")
set(spells "")
set(actions "")
foreach (t IN LISTS tuples)
  string(STRIP "${t}" t)
  if (NOT t MATCHES "^\\((3, 7|5, 2), ")
    message(FATAL_ERROR "#379 tuple for another race/class: ${t}")
  endif()
  if (t MATCHES "^\\([0-9]+, [0-9]+, [0-9]+, '[^']*'\\)$")
    string(REGEX REPLACE "^\\(([0-9]+), ([0-9]+), ([0-9]+), .*" "\\1.\\2.\\3" key "${t}")
    list(APPEND spells "${key}")
  elseif (t MATCHES "^\\([0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+\\)$")
    list(APPEND actions "${t}")
  elseif (t MATCHES "^\\([0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+, [0-9]+\\)$")
    list(APPEND levelstats "${t}")
  else()
    message(FATAL_ERROR "#379 tuple of unknown shape: ${t}")
  endif()
endforeach()

# Spells: the template's class spells with the race's racials and language swapped in.
set(expected_spells)
foreach (s 81 107 197 198 199 203 204 227 331 403 522 668 672 2382 2479 2481 3050 3365 6233 6246
           6247 6477 6478 6603 7266 7267 7355 8386 9077 9078 9116 9125 20594 20595 20596 21651
           21652 22027 22810 27763)
  list(APPEND expected_spells "3.7.${s}")
endforeach()
foreach (s 81 107 198 199 203 204 522 635 669 2382 2479 3050 3365 5227 6233 6246 6247 6477 6478
           6603 7266 7267 7355 7744 8386 8737 9077 9078 9116 9125 17737 20577 21084 21651 21652
           22027 22810 27762 52522)
  list(APPEND expected_spells "5.2.${s}")
endforeach()
require_equal("${spells}" "${expected_spells}" "#379 playercreateinfo_spell rows")
# Neither pair keeps the template race's racials or language.
foreach (s 669 20572 20573 20574 21563)
  list(FIND spells "3.7.${s}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "dwarf shaman kept orc spell ${s}")
  endif()
endforeach()
foreach (s 668 20597 20598 20599 20600 20864)
  list(FIND spells "5.2.${s}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "undead paladin kept human spell ${s}")
  endif()
endforeach()

set(expected_actions
  "(3, 7, 0, 6603, 0)" "(3, 7, 1, 403, 0)" "(3, 7, 2, 331, 0)" "(3, 7, 3, 20594, 0)"
  "(3, 7, 4, 2481, 0)" "(3, 7, 10, 159, 128)" "(3, 7, 11, 4540, 128)"
  "(5, 2, 0, 6603, 0)" "(5, 2, 1, 21084, 0)" "(5, 2, 2, 635, 0)" "(5, 2, 3, 20577, 0)"
  "(5, 2, 10, 159, 128)" "(5, 2, 11, 4604, 128)")
require_equal("${actions}" "${expected_actions}" "#379 playercreateinfo_action rows")

# Level stats: levels 1..60 exactly once per pair, in order, anchored at 1, 30 and 60.
set(n 0)
foreach (pair "3, 7" "5, 2")
  foreach (level RANGE 1 60)
    list(GET levelstats ${n} row)
    if (NOT row MATCHES "^\\(${pair}, ${level}, ")
      message(FATAL_ERROR "#379 player_levelstats row ${n} is ${row}, expected (${pair}, ${level}, ...)")
    endif()
    math(EXPR n "${n} + 1")
  endforeach()
endforeach()
list(LENGTH levelstats levelstats_count)
require_equal("${levelstats_count}" "120" "#379 player_levelstats row count")
foreach (anchor
    "(3, 7, 1, 23, 16, 24, 20, 21)" "(3, 7, 30, 48, 30, 53, 47, 52)" "(3, 7, 60, 87, 51, 98, 89, 99)"
    "(5, 2, 1, 21, 18, 23, 18, 26)" "(5, 2, 60, 104, 63, 101, 68, 80)")
  list(FIND levelstats "${anchor}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#379 player_levelstats anchor missing: ${anchor}")
  endif()
endforeach()

# Items: unkeyed table, so a pair's items go in only while it has none.
foreach (required
    "SELECT 3 AS `race`, 7 AS `class`, 36 AS `itemid`, 1 AS `amount`"
    "UNION ALL SELECT 3, 7, 153, 1"
    "UNION ALL SELECT 3, 7, 154, 1"
    "UNION ALL SELECT 3, 7, 159, 2"
    "UNION ALL SELECT 3, 7, 4540, 4"
    "UNION ALL SELECT 3, 7, 6948, 1"
    "WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo_item` i WHERE i.`race` = 3 AND i.`class` = 7);"
    "SELECT 5 AS `race`, 2 AS `class`, 43 AS `itemid`, 1 AS `amount`"
    "UNION ALL SELECT 5, 2, 44, 1"
    "UNION ALL SELECT 5, 2, 45, 1"
    "UNION ALL SELECT 5, 2, 159, 2"
    "UNION ALL SELECT 5, 2, 2361, 1"
    "UNION ALL SELECT 5, 2, 4604, 4"
    "UNION ALL SELECT 5, 2, 6948, 1"
    "WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo_item` i WHERE i.`race` = 5 AND i.`class` = 2);")
  require_text("${m}" "${required}" "#379 playercreateinfo_item")
endforeach()
string(REGEX MATCHALL "SELECT [0-9]+, [0-9]+, [0-9]+, [0-9]+\n" item_rows "${m}")
list(LENGTH item_rows item_count)
require_equal("${item_count}" "11" "#379 playercreateinfo_item UNION rows (plus the two first rows)")

# The pairs: the race's own start position, gated on complete level stats.
foreach (required
    "SELECT 3, 7, 0, 1, -6240.32, 331.033, 382.758, 6.17716 FROM DUAL\nWHERE (SELECT COUNT(*) FROM `player_levelstats` WHERE `race` = 3 AND `class` = 7 AND `level` BETWEEN 1 AND 60) = 60;"
    "SELECT 5, 2, 0, 85, 1676.35, 1677.45, 121.67, 2.70526 FROM DUAL\nWHERE (SELECT COUNT(*) FROM `player_levelstats` WHERE `race` = 5 AND `class` = 2 AND `level` BETWEEN 1 AND 60) = 60;"
    "`ok` tinyint(1) NOT NULL CHECK (`ok` = 1)"
    "(SELECT COUNT(*) FROM `playercreateinfo` WHERE (`race`, `class`) IN ((3, 7), (5, 2))) = 2")
  require_text("${m}" "${required}" "#379 playercreateinfo / fail-closed tail")
endforeach()

message(STATUS "RACE_CLASS_379_CONTRACT=PASS")
