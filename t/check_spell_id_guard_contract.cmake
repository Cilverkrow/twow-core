if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#484 (train 9): spell and item id guards.
# - Spell::cast() must fail a cast with an id above MAX_SPELL_ID (or an unknown id)
#   cleanly instead of a bare return that left the spell PREPARING forever.
# - Loading spell_template names every entry above the 16-bit client limit.
# - Cooldown and transaction item ids keep the full item entry (items above 65535
#   exist); only the 1.12 packet field stays 16 bit, with an explicit cast.
file(READ "${TW_CORE_ROOT}/src/game/Spells/Spell.h" spell_h)
file(READ "${TW_CORE_ROOT}/src/game/Spells/Spell.cpp" spell_cpp)
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellMgr.cpp" mgr)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Unit.h" unit_h)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player_cpp)
file(READ "${TW_CORE_ROOT}/src/game/World.h" world_h)
foreach (v spell_cpp mgr unit_h player_cpp world_h)
  string(REPLACE "\r" "" ${v} "${${v}}")
endforeach()

# 1. MAX_SPELL_ID is exactly the 16-bit client limit.
string(REGEX MATCH "#define MAX_SPELL_ID ([0-9]+)" _ "${spell_h}")
set(max_id "${CMAKE_MATCH_1}")
if (NOT max_id EQUAL 65535)
  message(FATAL_ERROR "MAX_SPELL_ID (${max_id}) must be 65535")
endif()

# 2. Spell::cast() guard: logged, failed and finished, after the bot cast-start hook.
string(FIND "${spell_cpp}" "void Spell::cast(bool skipCheck)" cast_at)
if (cast_at EQUAL -1)
  message(FATAL_ERROR "Spell::cast not found")
endif()
string(SUBSTRING "${spell_cpp}" ${cast_at} -1 cast_tail)
string(FIND "${cast_tail}" "SetExecutedCurrently(true);" exec_at)
if (exec_at EQUAL -1)
  message(FATAL_ERROR "Spell::cast: SetExecutedCurrently(true) not found")
endif()
string(SUBSTRING "${cast_tail}" 0 ${exec_at} head)
string(FIND "${head}" "BotActionLog_LogCastStart(m_caster" hook_at)
string(FIND "${head}" "m_spellInfo->Id > MAX_SPELL_ID" guard_at)
if (hook_at EQUAL -1 OR guard_at EQUAL -1 OR guard_at LESS hook_at)
  message(FATAL_ERROR "Spell::cast: MAX_SPELL_ID guard missing or before the bot cast-start hook")
endif()
string(SUBSTRING "${head}" ${guard_at} -1 guard)
set(prev -1)
foreach (needle
    "!spellKnown"
    "[SpellIdGuard] Spell::cast rejected spell"
    "SendCastResult(SPELL_FAILED_ERROR);"
    "finish(false);"
    "return;")
  string(FIND "${guard}" "${needle}" at)
  if (at EQUAL -1 OR NOT at GREATER prev)
    message(FATAL_ERROR "Spell::cast guard: '${needle}' missing or out of order")
  endif()
  set(prev ${at})
endforeach()
# No bare return before the guard's finish(false) (the pre-train-9 zombie cast).
if (head MATCHES "MAX_SPELL_ID\\)[ \t\n]*return;" OR head MATCHES "if \\(!spellInfo\\)[ \t\n]*return;")
  message(FATAL_ERROR "Spell::cast: bare return on an invalid spell id")
endif()

# 3. Load-time diagnostic for spell_template rows above the limit.
foreach (needle
    "if (spellId > MAX_SPELL_ID)"
    "[SpellIdGuard] spell_template entry %u is above the 16-bit spell id limit")
  string(FIND "${mgr}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "SpellMgr::LoadSpellsFromSpellTemplate: missing ${needle}")
  endif()
endforeach()

# 4. Full item ids in cooldowns and transaction logs.
if (NOT unit_h MATCHES "struct SpellCooldown\n{[^}]*uint32 itemid;")
  message(FATAL_ERROR "Unit.h: SpellCooldown::itemid must be uint32")
endif()
if (NOT world_h MATCHES "uint32 itemsEntries\\[MAX_TRANSACTION_ITEMS\\];")
  message(FATAL_ERROR "World.h: TransactionPart::itemsEntries must be uint32")
endif()
# The SMSG_INITIAL_SPELLS field is 16 bit in 1.12: the narrowing stays explicit.
string(FIND "${player_cpp}" "data << uint16(spellCooldown.second.itemid);" pkt_at)
if (pkt_at EQUAL -1)
  message(FATAL_ERROR "Player.cpp: SMSG_INITIAL_SPELLS item id must be written as explicit uint16")
endif()

message(STATUS "SPELL_ID_GUARD_CONTRACT=PASS max_spell_id=${max_id}")
