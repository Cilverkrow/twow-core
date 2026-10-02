if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 8.10 (twow-repo#484): own spells that players learn and that apply a lasting aura
# must be passive. Until train 9 sets the bit in the data, the server forces it for the
# listed IDs after loading the spells. The list must stay inside the custom range and be
# applied after both spell load paths.
file(READ "${TW_CORE_ROOT}/src/game/FunserverPassiveSpells.h" list_h)
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellMgr.cpp" mgr)
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world)

string(FIND "${list_h}" "FUNSERVER_FORCED_PASSIVE_SPELLS[] = {" list_at)
string(FIND "${list_h}" "};" list_end)
if (list_at EQUAL -1 OR list_end LESS list_at)
  message(FATAL_ERROR "passive spell list missing")
endif()
math(EXPR body_len "${list_end} - ${list_at}")
string(SUBSTRING "${list_h}" ${list_at} ${body_len} body)
string(REGEX REPLACE "//[^\n]*" "" body "${body}")
string(REGEX MATCHALL "[0-9][0-9][0-9][0-9][0-9]+" ids "${body}")
list(LENGTH ids n)
if (n EQUAL 0)
  message(FATAL_ERROR "passive spell list is empty")
endif()
foreach (id ${ids})
  if (id LESS 61002 OR id GREATER 61220)
    message(FATAL_ERROR "passive spell ${id} outside the custom range 61002-61220")
  endif()
endforeach()
# Shadow Dance R1-R3 (owner test 02.10.2026)
foreach (id 61143 61144 61145)
  list(FIND ids ${id} found)
  if (found EQUAL -1)
    message(FATAL_ERROR "Shadow Dance ${id} must be passive")
  endif()
endforeach()

foreach (needle
    "mSpellEntryMap[spellId]->Attributes |= SPELL_ATTR_PASSIVE;"
    "void SpellMgr::ApplyFunserverPassiveSpells()")
  string(FIND "${mgr}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "SpellMgr: missing ${needle}")
  endif()
endforeach()

# Applied after both load paths: after the else-branch with LoadSpells().
string(FIND "${world}" "sSpellMgr.LoadSpells();" load_at)
string(FIND "${world}" "sSpellMgr.ApplyFunserverPassiveSpells();" apply_at)
if (apply_at EQUAL -1 OR apply_at LESS load_at)
  message(FATAL_ERROR "World: ApplyFunserverPassiveSpells must run after the spells are loaded")
endif()

# Spell::cast() drops every id above MAX_SPELL_ID - it must cover the custom range.
file(READ "${TW_CORE_ROOT}/src/game/Spells/Spell.h" spell_h)
string(REGEX MATCH "#define MAX_SPELL_ID ([0-9]+)" _ "${spell_h}")
if (NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS 61220)
  message(FATAL_ERROR "MAX_SPELL_ID (${CMAKE_MATCH_1}) must cover the custom spells 61002-61220 (#484)")
endif()
if (CMAKE_MATCH_1 GREATER 65535)
  message(FATAL_ERROR "MAX_SPELL_ID (${CMAKE_MATCH_1}) above the 16-bit client limit")
endif()
message(STATUS "PASSIVE_SPELLS_CONTRACT=PASS spells=${n} max_spell_id=${CMAKE_MATCH_1}")
