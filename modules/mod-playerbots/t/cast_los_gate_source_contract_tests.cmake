# twow-repo#602 ROT-01 hunter LoS (focus check 25.10.2026, OB-00): AiPlayerbot.Rotation.CastLosGate (default 0, behaviour change) makes
# a cast IMPOSSIBLE when the bot's own CheckCast says LINE_OF_SIGHT (CanCastSpell tolerates it under ignoreRange), drops a current
# target that hotfix 8.2 marked unreachable, and lets target selection skip such units while another candidate exists.
# Pins: switch default 0; off = the old CanCastSpell call and the old FindTarget loop; the gate only on LINE_OF_SIGHT and only on a
# possible verdict; the drop only for the unreachable current target; the skip only with another candidate; CanCastSpell's own
# tolerance untouched; report; pure policy.
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/CastLosGatePolicy.h" policy)
read_file("${PB_SOURCE_DIR}/strategy/actions/GenericSpellActions.cpp" spell_cpp)
read_file("${PB_SOURCE_DIR}/strategy/values/TargetValue.cpp" target_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr_cpp)

# text from `start` up to (not including) `stop`, empty when either is missing
function(between text start stop out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${stop}" b)
  if(b EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

# append to `errors` of the calling function instead of failing (functions, not macros: macro arguments would
# re-evaluate the backslashes of the source text)
function(need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(errors "${errors};missing ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(ban text needle what)
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    set(errors "${errors};forbidden ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(errors "${errors};order ${what}: ${first} before ${second}" PARENT_SCOPE)
  endif()
endfunction()

function(check policy spell_cpp target_cpp ai_cpp config_h config_cpp dist mgr_cpp out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho].
  need("${dist}" "\nAiPlayerbot.Rotation.CastLosGate = 0\n" "dist key, default 0")
  need("${config_h}" "uint32 rotationCastLosGate = 0;" "config member, default 0")
  need("${config_cpp}" "rotationCastLosGate = config.GetIntDefault(\"AiPlayerbot.Rotation.CastLosGate\", 0);   // twow-repo#602 hunter LoS\n    if (rotationCastLosGate > 2)\n        rotationCastLosGate = 0;" "config read, > 2 = off")
  need("${config_cpp}" "{\"Rotation.CastLosGate\", Value(rotationCastLosGate)}," "parsed mode in [ConfigEcho] (owner rule #615)")

  # 2. Pure policy (std + the cohort rule only).
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
      list(APPEND errors "CastLosGatePolicy.h is not pure: ${inc}")
    endif()
  endforeach()
  need("${policy}" "return ai::cohort::Applies(mode, guidLow);" "cohort rule")
  need("${policy}" "return applies && unreachable && otherCandidate;" "skip only with another candidate")

  # 3. CastSpellAction::isPossible: off = the old call; the gate only turns a possible LINE_OF_SIGHT verdict into IMPOSSIBLE; the drop
  #    only for an 8.2-unreachable current target.
  between("${spell_cpp}" "bool CastSpellAction::isPossible()" "\nbool CastSpellAction::RotEval" isp)
  need("${isp}" "    if (ai::cast_los_gate::Applies(sPlayerbotAIConfig.rotationCastLosGate, bot->GetGUIDLow()))\n    {" "the gate block needs the switch")
  before("${isp}" "ai::cast_los_gate::Applies(sPlayerbotAIConfig.rotationCastLosGate" "    if (!sPlayerbotAIConfig.rotationCastTrace)\n\t    return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);" "gate block before the unchanged off path")
  need("${isp}" "    if (!sPlayerbotAIConfig.rotationCastTrace)\n\t    return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);" "off path = the old call (also pinned by the D0 contract)")
  need("${isp}" "if (losPossible && losResult == SPELL_FAILED_LINE_OF_SIGHT)\n        {\n            losPossible = false;" "gate only on a tolerated LINE_OF_SIGHT")
  need("${isp}" "if (ai->IsUnreachableTarget(spellTarget->GetObjectGuid()) && AI_VALUE(Unit*, \"current target\") == spellTarget)\n            {\n                SET_AI_VALUE(Unit*, \"current target\", nullptr);" "drop only the unreachable current target")
  need("${isp}" "return sPlayerbotAIConfig.rotationCastTrace ? RotEval(losPossible, ai::rot_cast::EvalCode(losPossible, uint8(losResult))) : losPossible;" "the gate keeps the D0 trace code")
  need("${isp}" "bool const possible = ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true, false, false, &result);\n    return RotEval(possible, ai::rot_cast::EvalCode(possible, uint8(result)));" "trace path unchanged")

  # 4. CanCastSpell's own tolerance (used by reach and others) is not touched.
  need("${ai_cpp}" "case SPELL_FAILED_LINE_OF_SIGHT:" "CanCastSpell keeps its LoS case")
  ban("${ai_cpp}" "rotationCastLosGate" "the switch inside PlayerbotAI.cpp")

  # 5. FindTarget: the skip needs the switch, an unreachable unit and another reachable candidate.
  between("${target_cpp}" "Unit* TargetValue::FindTarget(FindTargetStrategy* strategy)" "\n}\n" ft)
  need("${ft}" "if (!ai->IsUnreachableTarget(guid) && ai->GetUnit(guid))" "another reachable candidate")
  need("${ft}" "if (ai::cast_los_gate::SkipUnreachable(losGate, losGate && ai->IsUnreachableTarget(*i), reachable))" "skip rule")
  need("${ft}" "strategy->CheckAttacker(unit, &threatManager);" "old loop body kept")

  # 6. Report once per minute after [BotActivityMin], nothing when off.
  before("${mgr_cpp}" "    ReportBotActivity();" "    ReportCastLosGate();" "report after ReportBotActivity")
  need("${mgr_cpp}" "if (!sPlayerbotAIConfig.rotationCastLosGate || now < lastReport + 60)" "report only when on")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${spell_cpp}" "${target_cpp}" "${ai_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#602 cast los gate contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#602 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${spell_cpp}" "${target_cpp}" "${ai_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#602 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.Rotation.CastLosGate = 0\n" "\nAiPlayerbot.Rotation.CastLosGate = 1\n")
probe("unknown mode kept" config_cpp "    if (rotationCastLosGate > 2)\n        rotationCastLosGate = 0;" "")
probe("gate on every failure" spell_cpp "if (losPossible && losResult == SPELL_FAILED_LINE_OF_SIGHT)" "if (losResult != SPELL_CAST_OK)")
probe("drop any target" spell_cpp "if (ai->IsUnreachableTarget(spellTarget->GetObjectGuid()) && AI_VALUE(Unit*, \"current target\") == spellTarget)" "if (AI_VALUE(Unit*, \"current target\") == spellTarget)")
probe("gate without the switch" spell_cpp "    if (ai::cast_los_gate::Applies(sPlayerbotAIConfig.rotationCastLosGate, bot->GetGUIDLow()))\n    {" "    if (true)\n    {")
probe("off path changed" spell_cpp "    if (!sPlayerbotAIConfig.rotationCastTrace)\n\t    return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);" "    if (!sPlayerbotAIConfig.rotationCastTrace)\n\t    return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, false);")
probe("CanCastSpell changed" ai_cpp "case SPELL_FAILED_LINE_OF_SIGHT:" "case SPELL_FAILED_LINE_OF_SIGHT: if (sPlayerbotAIConfig.rotationCastLosGate) break;")
probe("skip the last candidate" policy "return applies && unreachable && otherCandidate;" "return applies && unreachable;")
probe("impure policy" policy "#include <cstdint>" "#include <cstdint>\n#include \"Entities/Player.h\"")
probe("report always" mgr_cpp "if (!sPlayerbotAIConfig.rotationCastLosGate || now < lastReport + 60)" "if (now < lastReport + 60)")

message(STATUS "cast_los_gate source contract passed (${probes} negative probes)")
