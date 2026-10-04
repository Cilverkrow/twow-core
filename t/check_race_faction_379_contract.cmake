if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#379 (hotfix 8.21): the Turtle races in faction and race lists, and the tauren
# shaman racial. Code only, no DB.
macro(require_in file text what)
  file(READ "${TW_CORE_ROOT}/${file}" body)
  string(FIND "${body}" "${text}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "${what}: missing in ${file}: ${text}")
  endif()
endmacro()

# Tauren shaman racial: Ethereal Form 45502; 47262 exists in no spell_template.
require_in("src/scripts/miscellaneous/random_scripts_3.cpp" "SPELL_ETHEREAL_FORM = 45502" "tauren racial")
require_in("src/scripts/miscellaneous/random_scripts_3.cpp" "pPlayer->LearnSpell(SPELL_ETHEREAL_FORM, false);" "tauren racial")
file(READ "${TW_CORE_ROOT}/src/scripts/miscellaneous/random_scripts_3.cpp" scripts)
string(REGEX REPLACE "//[^\n]*" "" scripts_code "${scripts}")
string(FIND "${scripts_code}" "47262" old_id)
if (NOT old_id EQUAL -1)
  message(FATAL_ERROR "tauren racial: the non-existent spell 47262 is back in code")
endif()

# High elf counts as Alliance for the bots (core mask, not a hand-written list).
require_in("src/game/SharedDefines.h" "(1<<(RACE_GNOME-1))     |(1<<(RACE_HIGH_ELF-1)))" "RACEMASK_ALLIANCE")
require_in("modules/mod-playerbots/src/playerbot/PlayerbotAI.cpp"
  "return race > 0 && race < MAX_RACES && ((1u << (race - 1)) & RACEMASK_ALLIANCE) != 0;" "IsAlliance")

# Goblin and high elf map onto each other; Turtle race names for GM commands and bot texts.
require_in("src/game/ObjectMgr.cpp" "case RACE_GOBLIN:\n            return RACE_HIGH_ELF;" "GetOppositeRace")
require_in("src/game/ObjectMgr.cpp" "case RACE_HIGH_ELF:\n            return RACE_GOBLIN;" "GetOppositeRace")
require_in("src/game/Chat/Chat.cpp" "{ \"highelf\", (1 << (RACE_HIGH_ELF - 1)) }," "race mask names")
require_in("src/game/Chat/Chat.cpp" "{ \"goblin\", (1 << (RACE_GOBLIN - 1))  }," "race mask names")
require_in("modules/mod-playerbots/src/playerbot/ChatHelper.cpp" "races[RACE_HIGH_ELF] = \"High Elf\";" "bot race names")

message(STATUS "RACE_FACTION_379_CONTRACT=PASS")
