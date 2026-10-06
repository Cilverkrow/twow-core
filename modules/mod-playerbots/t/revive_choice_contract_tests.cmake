# Hotfix 8.34 (twow-repo#544): corpse run before the spirit-healer shortcuts (switch, default off).
file(READ "${PB_SOURCE_DIR}/strategy/values/DeadValues.cpp" dead)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "dead|bool const shortcutAllowed = revive_choice::ShortcutToSpiritHealerAllowed(sPlayerbotAIConfig.revivePreferCorpseRun,"
    "dead|if (shortcutAllowed && AI_VALUE2(bool, \"manual bool\", \"enemies near corpse\"))"
    "dead|if (corpseInSight && shortcutAllowed)"
    "dead|if (shortcutAllowed && graveInSight && !corpseInSight && ai->HasCheat(BotCheatMask::repair))"
    "config_cpp|AiPlayerbot.Revive.PreferCorpseRun\", false"
    "config_dist|AiPlayerbot.Revive.PreferCorpseRun = 0")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "revive choice: missing ${needle}")
  endif()
endforeach()
# The ungated grave-in-sight shortcut must be gone.
string(FIND "${dead}" "    if (graveInSight && !corpseInSight && ai->HasCheat(BotCheatMask::repair))" ungated)
if (NOT ungated EQUAL -1)
  message(FATAL_ERROR "revive choice: ungated graveyard shortcut still present")
endif()
message(STATUS "REVIVE_CHOICE_CONTRACT=PASS")
