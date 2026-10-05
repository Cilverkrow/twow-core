if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 8.10 (twow-repo#484): own spells that players learn and that apply a lasting aura
# must be passive. Until train 9 sets the bit in the data, the server forces it for the
# listed IDs after loading the spells. The list must stay inside the custom range and be
# applied after both spell load paths.
# Train 9 (twow-repo#484): the data carries the bit since 20261003200000; the list stays as a
# safeguard. The custom range is 61002-65535 (new IDs from 61221, riding 61300-61399).
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
string(REGEX MATCH "FUNSERVER_CUSTOM_SPELL_MIN = ([0-9]+);" _ "${list_h}")
set(range_min "${CMAKE_MATCH_1}")
string(REGEX MATCH "FUNSERVER_CUSTOM_SPELL_MAX = ([0-9]+);" _ "${list_h}")
set(range_max "${CMAKE_MATCH_1}")
if (NOT range_min EQUAL 61002 OR NOT range_max EQUAL 65535)
  message(FATAL_ERROR "custom spell range must be 61002-65535 (got '${range_min}'-'${range_max}')")
endif()
foreach (id ${ids})
  if (id LESS range_min OR id GREATER range_max)
    message(FATAL_ERROR "passive spell ${id} outside the custom range ${range_min}-${range_max}")
  endif()
endforeach()
# Train 9: the comment must say why the list is still there.
string(FIND "${list_h}" "The list stays as a safeguard" safeguard_at)
if (safeguard_at EQUAL -1)
  message(FATAL_ERROR "FunserverPassiveSpells.h: missing the train-9 safeguard note")
endif()
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
if (NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS range_max)
  message(FATAL_ERROR "MAX_SPELL_ID (${CMAKE_MATCH_1}) must cover the custom spells ${range_min}-${range_max} (#484)")
endif()
if (CMAKE_MATCH_1 GREATER 65535)
  message(FATAL_ERROR "MAX_SPELL_ID (${CMAKE_MATCH_1}) above the 16-bit client limit")
endif()
message(STATUS "PASSIVE_SPELLS_CONTRACT=PASS spells=${n} max_spell_id=${CMAKE_MATCH_1}")
