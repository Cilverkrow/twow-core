if(NOT DEFINED PB_MODULE_DIR OR NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "PB_MODULE_DIR and TW_CORE_ROOT are required")
endif()

# twow-repo#379: the bot factory's race/class policy.
#
# 1. The vanilla build's allow-list is exactly the reviewed set below: the
#    combinations the factory already had plus dwarf shaman (3,7) and undead
#    paladin (5,2). A pair added or dropped without updating this list fails.
# 2. Every allowed pair has playercreateinfo data (sql/base plus the migrations
#    that add pairs), because Player::Create refuses a pair without it.
# 3. The group-fill command asks the factory instead of hardcoding "no Horde
#    paladin, no Alliance shaman".

file(READ "${PB_MODULE_DIR}/src/playerbot/RandomPlayerbotFactory.cpp" factory)
file(READ "${PB_MODULE_DIR}/src/playerbot/RandomPlayerbotFactory.h" factory_header)
file(READ "${PB_MODULE_DIR}/src/playerbot/PlayerbotMgr.cpp" mgr)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

set(RACE_HUMAN 1)
set(RACE_ORC 2)
set(RACE_DWARF 3)
set(RACE_NIGHTELF 4)
set(RACE_UNDEAD 5)
set(RACE_TAUREN 6)
set(RACE_GNOME 7)
set(RACE_TROLL 8)
set(RACE_GOBLIN 9)
set(RACE_HIGH_ELF 10)
set(CLASS_WARRIOR 1)
set(CLASS_PALADIN 2)
set(CLASS_HUNTER 3)
set(CLASS_ROGUE 4)
set(CLASS_PRIEST 5)
set(CLASS_SHAMAN 7)
set(CLASS_MAGE 8)
set(CLASS_WARLOCK 9)
set(CLASS_DRUID 11)

# --- 1. the constructor's allow-list as the vanilla build sees it
string(FIND "${factory}" "RandomPlayerbotFactory::RandomPlayerbotFactory(uint32 accountId)" ctor_begin)
string(FIND "${factory}" "bool RandomPlayerbotFactory::isAvailableRace" ctor_end)
if(ctor_begin EQUAL -1 OR ctor_end EQUAL -1 OR ctor_end LESS ctor_begin)
  message(FATAL_ERROR "RandomPlayerbotFactory constructor not found")
endif()
math(EXPR ctor_len "${ctor_end} - ${ctor_begin}")
string(SUBSTRING "${factory}" ${ctor_begin} ${ctor_len} ctor)
# Drop the blocks a MANGOSBOT_ZERO build preprocesses away. None of them nests.
string(REGEX REPLACE "#ifndef MANGOSBOT_ZERO[^#]*#endif" "" ctor "${ctor}")
string(REGEX REPLACE "#ifdef MANGOSBOT_TWO[^#]*#endif" "" ctor "${ctor}")

string(REGEX MATCHALL "availableRaces\\[CLASS_[A-Z_]+\\]\\.push_back\\(RACE_[A-Z_]+\\)" pushes "${ctor}")
set(actual "")
foreach(push IN LISTS pushes)
  string(REGEX REPLACE "availableRaces\\[(CLASS_[A-Z_]+)\\]\\.push_back\\((RACE_[A-Z_]+)\\)" "\\1;\\2" parts "${push}")
  list(GET parts 0 cls_name)
  list(GET parts 1 race_name)
  if(NOT DEFINED ${cls_name} OR NOT DEFINED ${race_name})
    message(FATAL_ERROR "Unknown constant in ${push}")
  endif()
  list(APPEND actual "${${race_name}},${${cls_name}}")
endforeach()
list(REMOVE_DUPLICATES actual)
list(SORT actual)

# race,class
set(expected
  # warrior
  1,1 4,1 7,1 3,1 2,1 5,1 6,1 8,1 9,1 10,1
  # paladin, incl. #379 undead paladin
  1,2 3,2 10,2 5,2
  # rogue
  1,4 3,4 4,4 7,4 2,4 5,4 8,4 9,4 10,4
  # priest, incl. #379 follow-up tauren priest
  1,5 3,5 4,5 8,5 5,5 10,5 6,5
  # mage, incl. #379 follow-up orc and dwarf mage
  1,8 7,8 5,8 8,8 9,8 10,8 2,8 3,8
  # warlock, incl. #379 follow-up dwarf and troll warlock
  1,9 7,9 5,9 2,9 9,9 3,9 8,9
  # shaman, incl. #379 dwarf shaman
  2,7 6,7 8,7 3,7
  # hunter, incl. #379 follow-up undead and gnome hunter
  1,3 3,3 4,3 2,3 6,3 8,3 9,3 10,3 5,3 7,3
  # druid
  4,11 6,11)
string(REPLACE " " ";" expected "${expected}")
list(SORT expected)

if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "Factory race/class allow-list changed.\n  actual:   ${actual}\n  expected: ${expected}")
endif()

# --- 2. every allowed pair has playercreateinfo rows
file(READ "${TW_CORE_ROOT}/sql/base/tw_world_playercreateinfo.sql" base_pci)
string(REGEX MATCHALL "\\(([0-9]+),([0-9]+),[0-9]+,[0-9]+,-?[0-9.]+,-?[0-9.]+,-?[0-9.]+,-?[0-9.]+\\)" base_rows "${base_pci}")
set(data_pairs "")
foreach(row IN LISTS base_rows)
  string(REGEX REPLACE "^\\(([0-9]+),([0-9]+),.*" "\\1,\\2" pair "${row}")
  list(APPEND data_pairs "${pair}")
endforeach()
# Pairs added by migrations: dwarf warlock / tauren priest, then #379.
file(READ "${TW_CORE_ROOT}/sql/database_updates/world/20260517101017_world.sql" m517)
string(REGEX MATCHALL "\\(([0-9]+), ([0-9]+), [0-9]+, [0-9]+, -?[0-9.]+, -?[0-9.]+, -?[0-9.]+, -?[0-9.]+\\)" m517_rows "${m517}")
foreach(row IN LISTS m517_rows)
  string(REGEX REPLACE "^\\(([0-9]+), ([0-9]+),.*" "\\1,\\2" pair "${row}")
  list(APPEND data_pairs "${pair}")
endforeach()
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927120000_world.sql" m379)
string(REGEX MATCHALL "SELECT ([0-9]+), ([0-9]+), [0-9]+, [0-9]+, -?[0-9.]+, -?[0-9.]+, -?[0-9.]+, -?[0-9.]+ FROM DUAL" m379_rows "${m379}")
foreach(row IN LISTS m379_rows)
  string(REGEX REPLACE "^SELECT ([0-9]+), ([0-9]+),.*" "\\1,\\2" pair "${row}")
  list(APPEND data_pairs "${pair}")
endforeach()

foreach(pair IN LISTS actual)
  list(FIND data_pairs "${pair}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Factory allows race,class ${pair} but no playercreateinfo row adds it")
  endif()
endforeach()

# --- 3. group fill asks the factory
require_text("${factory_header}" "static bool isClassForTeam(uint8 cls, Team team);" "isClassForTeam declaration")
require_text("${factory}" "if (isRaceForTeam(race, team) && isAvailableRace(cls, race))" "isClassForTeam uses the allow-list")
require_text("${mgr}" "if (!RandomPlayerbotFactory::isClassForTeam(cls, team))" "group fill asks the factory")
forbid_text("${mgr}" "cls == CLASS_PALADIN && team == HORDE" "hardcoded Horde paladin exclusion")
forbid_text("${mgr}" "cls == CLASS_SHAMAN && team == ALLIANCE" "hardcoded Alliance shaman exclusion")

message(STATUS "RACE_CLASS_AVAILABILITY_CONTRACT=PASS")
