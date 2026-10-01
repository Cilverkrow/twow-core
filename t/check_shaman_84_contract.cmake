if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 8.4 (twow-repo#357): Ancestral Arms teaches its spells to players; every charge
# change of an aura reaches its script (Shield Constitution / Shield Ward follow the charges).
file(READ "${TW_CORE_ROOT}/src/game/FunserverTalentLearnSpells.h" table)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/CharacterHandler.cpp" login)
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellAuras.cpp" auras)
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellAuras.h" auras_h)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Unit.cpp" unit)

foreach (pair
    "table|{ 61131, { 201, 202, 61132 } }"
    "player|LearnFunserverTalentSpells(this)"
    "player|player->LearnSpell(spellId, false)"
    "login|LearnFunserverTalentSpells(pCurrChar)"
    "auras_h|void SetAuraCharges(uint32 charges)"
    "auras_h|bool DropAuraCharge()")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 var)
  list(GET parts 1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Hotfix 8.4: missing in ${var}: ${needle}")
  endif()
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
