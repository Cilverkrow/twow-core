if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#527 (hotfix 8.23): tauren shamans who finished quest 40348 before hotfix 8.21 learn
# Ethereal Form 45502 at login; only with the quest rewarded, only when the spell is missing.
macro(require_in file text what)
  file(READ "${TW_CORE_ROOT}/${file}" body)
  string(FIND "${body}" "${text}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "${what}: missing in ${file}: ${text}")
  endif()
endmacro()

require_in("src/game/FunserverQuestSpellRegrant.h" "{ 40348, 1u << (6 - 1), 1u << (7 - 1), 45502 }," "tauren shaman regrant row")
require_in("src/game/Objects/Player.cpp"
  "if (!player->GetQuestRewardStatus(entry.questId) || player->HasSpell(entry.spellId))" "regrant only rewarded and missing")
require_in("src/game/Objects/Player.cpp"
  "if (!(player->GetRaceMask() & entry.raceMask) || !(player->GetClassMask() & entry.classMask))" "regrant only for race and class")
require_in("src/game/Handlers/CharacterHandler.cpp" "RegrantFunserverQuestSpells(pCurrChar);" "regrant at login")

message(STATUS "QUEST_SPELL_REGRANT_527_CONTRACT=PASS")
