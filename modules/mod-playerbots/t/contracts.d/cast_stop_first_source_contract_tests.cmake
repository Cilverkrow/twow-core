# twow-repo#602 ROT-01 cast-abort loop (OB-00 27.10.2026): AiPlayerbot.Rotation.CastStopFirst, code default 1 (0 = rollback),
# lets a moving bot without a real player master cast a cast-time or channelled spell after StopMoving instead of cancelling it
# (hunt2 action logs: 47k CAST_FAIL reason=none state=null, 740 streaks of 10 or more without a cast). The decision in the three
# PlayerbotAI::CastSpell overloads is pinned by loot_cast_after_stop_source_contract_tests.cmake; this contract pins the switch:
# dist key, config member and read (unknown modes = the default 1), the parsed mode in [ConfigEcho] (owner rule #615), the pure
# policy, the position overload's start check and the [CastStopFirst] minute line. The rules are tested in
# t/cast_while_moving_policy_tests.cpp.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#602 CastStopFirst: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("CastWhileMovingPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; parsed mode in [ConfigEcho].
require_text("${dist}" "\nAiPlayerbot.Rotation.CastStopFirst = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 rotationCastStopFirst = 1;" "config member, default 1")
require_text("${config_cpp}" "rotationCastStopFirst = config.GetIntDefault(\"AiPlayerbot.Rotation.CastStopFirst\", 1);   // twow-repo#602 cast-abort loop\n    if (rotationCastStopFirst > 2)\n        rotationCastStopFirst = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "            {\"Rotation.CastLosGate\", Value(rotationCastLosGate)},\n            {\"Rotation.CastStopFirst\", Value(rotationCastStopFirst)},\n            {\"Rotation.DarkRuneGate\"" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only).
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 CastStopFirst: CastWhileMovingPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "inline bool CastStopFirstApplies(std::uint32_t mode, std::uint32_t guidLow)\n{\n    return ai::cohort::Applies(mode, guidLow);\n}" "the cohort rule")

# 3. The position overload ignored a failed SpellStart (returned true with a cast wait); with the switch it returns false.
read_source("PlayerbotAI.cpp" ai_cpp)
set(pos_head "bool PlayerbotAI::CastSpell(uint32 spellId, float x, float y, float z, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)")
string(FIND "${ai_cpp}" "${pos_head}" pos_at)
if(pos_at EQUAL -1)
  message(FATAL_ERROR "#602 CastStopFirst: the position overload of PlayerbotAI::CastSpell not found")
endif()
string(SUBSTRING "${ai_cpp}" ${pos_at} -1 pos_body)
require_text("${pos_body}" "    SpellCastResult const startResult = spell->SpellStart(&targets);\n    if (startResult != SPELL_CAST_OK && ai::cast_moving::CastStopFirstApplies(sPlayerbotAIConfig.rotationCastStopFirst, bot->GetGUIDLow()))\n        return false;\n\n    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||" "a failed start returns false in the position overload")
string(FIND "${pos_body}" "\n    spell->SpellStart(&targets);\n" bare)
string(FIND "${pos_body}" "SpellCastResult const startResult = spell->SpellStart(&targets);" checked)
if(NOT bare EQUAL -1 AND bare LESS checked)
  message(FATAL_ERROR "#602 CastStopFirst: the position overload still has an unchecked SpellStart")
endif()

# 4. The [CastStopFirst] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/CastWhileMovingPolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "    if (!sPlayerbotAIConfig.rotationCastStopFirst || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${mgr_cpp}" "sLog.outBasic(\"[CastStopFirst] mode=%u cast_after_stop=%llu cancelled=%llu\", sPlayerbotAIConfig.rotationCastStopFirst,\n        (unsigned long long)ai::cast_moving::castAfterStop.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::cast_moving::cancelled.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportCastStopFirst();" "the minute report is called")

message(STATUS "#602 CastStopFirst source contract passed")
