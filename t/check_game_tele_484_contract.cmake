cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0057 NEW)

if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#484 train 9 (owner list point 9): GM .tele marks on the 11 maps without a client
# Map.dbc entry are removed, with a backup table first. Maps 44 and 806 belong to core#252
# (game_tele 621, 622, 809, 810, 827, 811 are redirected there and must never be touched here).
set(migration "${TW_CORE_ROOT}/sql/database_updates/20261003201000_world.sql")
if (NOT EXISTS "${migration}")
  message(FATAL_ERROR "#484 game_tele: missing ${migration}")
endif()
# HEX read: a plain file(READ) on Windows hosts drops CR bytes. Byte-aligned search for 0d.
file(READ "${migration}" raw_hex HEX)
if (raw_hex MATCHES "^(..)*0d")
  message(FATAL_ERROR "#484 game_tele: migration must use LF line endings")
endif()
file(READ "${migration}" raw)

# Executable SQL only: header comments (rollback text, mark list) must not satisfy or break checks.
file(STRINGS "${migration}" lines)
set(sql "")
foreach (line IN LISTS lines)
  if (NOT line MATCHES "^[ \t]*--")
    string(APPEND sql "${line}\n")
  endif()
endforeach()

# Header: rollback and owner reference stay documented.
foreach (required
    "--   INSERT IGNORE INTO `game_tele` SELECT * FROM `game_tele_bak_484`"
    "issuecomment-5972156860")
  string(FIND "${raw}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#484 game_tele: header lacks: ${required}")
  endif()
endforeach()

# Backup before the DELETE, the DELETE only through the backup (id and map), end-state CHECK last.
# List items must not contain ";" (the CMake list separator), so statements are matched without it.
set(order
    "CREATE TABLE IF NOT EXISTS `game_tele_bak_484` LIKE `game_tele`"
    "INSERT IGNORE INTO `game_tele_bak_484`"
    "DELETE t FROM `game_tele` t\n  JOIN `game_tele_bak_484` b ON b.`id` = t.`id` AND b.`map` = t.`map`"
    "CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_484_tele` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1))"
    "INSERT INTO `tmp_check_484_tele` (`ok`)")
set(last -1)
foreach (required IN LISTS order)
  string(FIND "${sql}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#484 game_tele: missing statement: ${required}")
  endif()
  if (NOT at GREATER last)
    message(FATAL_ERROR "#484 game_tele: statement out of order: ${required}")
  endif()
  set(last ${at})
endforeach()

string(TOUPPER "${sql}" sql_upper)
string(REGEX MATCHALL "DELETE " deletes "${sql_upper}")
list(LENGTH deletes delete_count)
if (NOT delete_count EQUAL 1)
  message(FATAL_ERROR "#484 game_tele: exactly one DELETE (through the backup) is approved, got ${delete_count}")
endif()
foreach (forbidden "UPDATE " "REPLACE " "DROP " "TRUNCATE " "ALTER " "`CREATURE`" "`GAMEOBJECT`" "`MAP_TEMPLATE`"
                   "`AREATRIGGER_TELEPORT`" "`GAME_TELE_BAK_427`" "`GAME_TELE_BAK_459`")
  string(FIND "${sql_upper}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "#484 game_tele: forbidden scope: ${forbidden}")
  endif()
endforeach()

# Exactly the 41 approved (id, map) pairs, nothing else, and never a core#252 mark.
set(expected
    "413,13" "502,13" "699,13"
    "548,25" "550,25" "557,25" "566,25" "567,25" "568,25" "586,25" "623,25"
    "503,29" "706,31"
    "415,37" "431,37" "527,37" "528,37" "529,37"
    "504,42" "512,42" "518,42" "519,42" "524,42" "538,42" "576,42" "577,42" "583,42" "822,42"
    "608,49" "609,49" "610,50"
    "560,150" "564,150" "565,150" "579,150" "581,150" "582,150" "613,150"
    "705,804" "820,809" "821,809")
string(REGEX MATCH "INSERT IGNORE INTO `game_tele_bak_484`[^;]*;" backup_stmt "${sql}")
string(REGEX MATCHALL "\\([0-9]+, [0-9]+\\)" pairs "${backup_stmt}")
set(found "")
foreach (pair IN LISTS pairs)
  string(REGEX REPLACE "^\\(([0-9]+), ([0-9]+)\\)$" "\\1,\\2" pair "${pair}")
  list(APPEND found "${pair}")
endforeach()
list(LENGTH found found_count)
list(REMOVE_DUPLICATES found)
list(LENGTH found unique_count)
if (NOT found_count EQUAL 41 OR NOT unique_count EQUAL 41)
  message(FATAL_ERROR "#484 game_tele: backup must list 41 distinct (id, map) pairs, got ${found_count} (${unique_count} distinct)")
endif()
foreach (pair IN LISTS expected)
  if (NOT pair IN_LIST found)
    message(FATAL_ERROR "#484 game_tele: approved pair missing: (${pair})")
  endif()
endforeach()
foreach (pair IN LISTS found)
  if (NOT pair IN_LIST expected)
    message(FATAL_ERROR "#484 game_tele: pair outside the approved list: (${pair})")
  endif()
  string(REGEX REPLACE ",.*$" "" pair_id "${pair}")
  string(REGEX REPLACE "^.*," "" pair_map "${pair}")
  if (pair_id MATCHES "^(621|622|809|810|827|811)$")
    message(FATAL_ERROR "#484 game_tele: game_tele ${pair_id} belongs to core#252")
  endif()
  if (pair_map MATCHES "^(44|806)$")
    message(FATAL_ERROR "#484 game_tele: map ${pair_map} belongs to core#252")
  endif()
endforeach()

# The end-state CHECK covers exactly the 11 maps and guards the core#252 marks.
foreach (required
    "SELECT (SELECT COUNT(*) FROM `game_tele` WHERE `map` IN (13, 25, 29, 31, 37, 42, 49, 50, 150, 804, 809)) = 0"
    "WHERE `map` IN (13, 25, 29, 31, 37, 42, 49, 50, 150, 804, 809)) = 41"
    "FROM `game_tele_bak_484` WHERE `id` IN (621, 622, 809, 810, 827, 811)) = 0"
    "AND (SELECT COUNT(*) FROM `game_tele` WHERE `map` = 45) = 0;")
  string(FIND "${sql}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#484 game_tele: end-state CHECK lacks: ${required}")
  endif()
endforeach()

message(STATUS "GAME_TELE_484_CONTRACT=PASS")
