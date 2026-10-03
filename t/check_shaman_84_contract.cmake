if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 8.4 (twow-repo#357): every charge change of an aura reaches its script (Shield
# Constitution / Shield Ward follow the charges).
# Train 9 (twow-repo#484): Ancestral Arms teaches its spells through spell_learn_spell
# (20261003200500_world.sql); the 8.4 workaround FunserverTalentLearnSpells.h is gone.
set(migration "${TW_CORE_ROOT}/sql/database_updates/20261003200500_world.sql")
file(READ "${migration}" m)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/CharacterHandler.cpp" login)
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellAuras.cpp" auras)
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellAuras.h" auras_h)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Unit.cpp" unit)

foreach (pair
    "m|INSERT IGNORE INTO `spell_learn_spell` (`entry`, `SpellID`, `Active`) VALUES"
    "m|(61131, 201, 1)"
    "m|(61131, 202, 1)"
    "m|(61131, 61132, 1)"
    "auras_h|void SetAuraCharges(uint32 charges)"
    "auras_h|bool DropAuraCharge()")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 var)
  list(GET parts 1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Shaman 8.4/#484: missing in ${var}: ${needle}")
  endif()
endforeach()

# The workaround is removed completely (header, the talent-learn call and the login call).
if (EXISTS "${TW_CORE_ROOT}/src/game/FunserverTalentLearnSpells.h")
  message(FATAL_ERROR "#484: src/game/FunserverTalentLearnSpells.h must be gone (spell_learn_spell replaces it)")
endif()
foreach (var player login)
  foreach (needle "FunserverTalentLearnSpells" "LearnFunserverTalentSpells")
    string(FIND "${${var}}" "${needle}" at)
    if (NOT at EQUAL -1)
      message(FATAL_ERROR "#484: the 8.4 workaround is still referenced in ${var}: ${needle}")
    endif()
  endforeach()
endforeach()

# Both charge setters notify the script.
string(REGEX MATCHALL "if \\(m_auraScript\\)[\r\n ]+m_auraScript->OnAuraChargesChanged\\(this\\)" hooks "${auras}")
list(LENGTH hooks hook_count)
if (NOT hook_count EQUAL 2)
  message(FATAL_ERROR "Hotfix 8.4: SetAuraCharges and DropAuraCharge must both notify the aura script (found ${hook_count})")
endif()
# No second explicit call in the proc path (it would notify twice).
string(FIND "${unit}" "triggeredByHolder->GetAuraScript()->OnAuraChargesChanged" twice)
if (NOT twice EQUAL -1)
  message(FATAL_ERROR "Hotfix 8.4: the proc path must not call OnAuraChargesChanged a second time")
endif()

message(STATUS "SHAMAN_84_CONTRACT=PASS")
