# twow-repo#602 ROT-01 (L1-b finding 19.10.2026, OB-10): AiPlayerbot.CastWait.Preparing (default 0, behaviour change) holds the
# internal AI until the real end of the bot's own PREPARING cast instead of re-running the engine against it every ReactDelay.
# Pins: switch default 0 and checked first; the CASTING branch unchanged; the release check before the reaction/internal gate;
# the OB-10 guards (deadline once per spell, never shorten, release back to the pending delay); cohort mode; report; pure policy.
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/CastWaitPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
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
function(count text needle expected what)
  string(LENGTH "${needle}" len)
  set(n 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    set(errors "${errors};${what}: expected ${expected} x ${needle}, found ${n}" PARENT_SCOPE)
  endif()
endfunction()

function(check policy ai_cpp ai_h config_h config_cpp dist mgr_cpp out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist, the member and the read; unknown modes are off.
  need("${dist}" "\nAiPlayerbot.CastWait.Preparing = 0\n" "dist key, default 0")
  need("${config_h}" "uint32 castWaitPreparing = 0;" "config member, default 0")
  need("${config_cpp}" "castWaitPreparing = config.GetIntDefault(\"AiPlayerbot.CastWait.Preparing\", 0);\n    if (castWaitPreparing > 2)\n        castWaitPreparing = 0;" "config read, > 2 = off")

  # 2. Pure policy (std only).
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>")
      list(APPEND errors "CastWaitPolicy.h is not pure: ${inc}")
    endif()
  endforeach()

  # 3. UpdateAI: the release check runs before the reaction/internal gate, switch first; it restores the pending delay.
  between("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" "\nbool PlayerbotAI::UpdateAIReaction(" update)
  before("${update}" "if (sPlayerbotAIConfig.castWaitPreparing && ai::cast_wait::Active(castWait))" "if(!UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()) && CanUpdateAIInternal())" "release check before the gate")
  need("${update}" "aiInternalUpdateDelay = ai::cast_wait::ReleasedDelay(castWait, WorldTimer::getMSTime(), aiInternalUpdateDelay);" "release back to the pending delay (OB-10 guard 3)")
  need("${update}" "castSpell && castSpell->getState() == SPELL_STATE_PREPARING))" "release when the spell is no longer PREPARING")

  # 4. The CASTING branch is unchanged; the PREPARING branch follows it, switch first, cohort mode, never shortening.
  need("${update}" "if (currentSpell && (currentSpell->getState() == SPELL_STATE_CASTING) && (currentSpell->GetCastedTime() > 0U))\n        {\n            SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());" "CASTING branch unchanged")
  before("${update}" "SPELL_STATE_CASTING) && (currentSpell->GetCastedTime() > 0U))" "else if (sPlayerbotAIConfig.castWaitPreparing && currentSpell && currentSpell->getState() == SPELL_STATE_PREPARING" "PREPARING branch after CASTING")
  need("${update}" "ai::cohort::Applies(sPlayerbotAIConfig.castWaitPreparing, bot->GetGUIDLow()) &&\n            !HasRealPlayerMaster())" "cohort mode, not for bots of a real player")
  need("${update}" "sPlayerbotAIConfig.reactDelay, sWorld.GetAverageDiff(),\n                aiInternalUpdateDelay);" "Extend gets the pending delay (OB-10 guard 2)")
  need("${update}" "aiInternalUpdateDelay = grant.delay;" "the granted delay")
  count("${ai_cpp}" "ai::cast_wait::Extend(" 1 "one Extend call")
  need("${ai_h}" "ai::cast_wait::Watch castWait;" "per-bot watch")

  # 5. Policy guards: deadline fixed only at a new sighting; max(wait, pending); a later foreign delay survives a release.
  between("${policy}" "inline Grant Extend(" "\n}\n" extend)
  between("${extend}" "if (w.spell != spell || w.castTime != castTimeMs)" "g.firstSighting = true;" sighting)
  need("${sighting}" "w.deadline = now + CapMs(castTimeMs);" "deadline set at the first sighting (OB-10 guard 1)")
  count("${policy}" "w.deadline = " 1 "deadline assigned once")
  need("${extend}" "g.delay = wait == 0 ? 0u : (wait > currentDelay ? wait : currentDelay);" "never shorten (OB-10 guard 2)")
  need("${policy}" "if (currentDelay > Until(now, w.grantEnd) + 5u)\n        return currentDelay;" "release keeps a later foreign delay")
  need("${policy}" "return twice > plus ? twice : plus;" "cap = max(2 x cast, cast + 1500)")

  # 6. Report once per minute next to [CombatIdle], nothing when off.
  before("${mgr_cpp}" "    ReportCombatIdle();" "    ReportCastWait();" "report after ReportCombatIdle")
  need("${mgr_cpp}" "if (!sPlayerbotAIConfig.castWaitPreparing || now < lastReport + 60)" "report only when on")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#602 cast wait contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#602 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#602 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.CastWait.Preparing = 0\n" "\nAiPlayerbot.CastWait.Preparing = 1\n")
probe("unknown mode kept" config_cpp "    if (castWaitPreparing > 2)\n        castWaitPreparing = 0;" "")
probe("deadline per call" policy "    std::uint32_t const want = timerMs" "    w.deadline = now + CapMs(castTimeMs);\n    std::uint32_t const want = timerMs")
probe("shortens a delay" policy "g.delay = wait == 0 ? 0u : (wait > currentDelay ? wait : currentDelay);" "g.delay = wait;")
probe("release resets to 0" ai_cpp "aiInternalUpdateDelay = ai::cast_wait::ReleasedDelay(castWait, WorldTimer::getMSTime(), aiInternalUpdateDelay);" "aiInternalUpdateDelay = 0;")
probe("release after the gate" ai_cpp "    if (sPlayerbotAIConfig.castWaitPreparing && ai::cast_wait::Active(castWait))" "    if (ai::cast_wait::Active(castWait))")
probe("CASTING branch changed" ai_cpp "(currentSpell->getState() == SPELL_STATE_CASTING) && (currentSpell->GetCastedTime() > 0U))" "(currentSpell->getState() >= SPELL_STATE_PREPARING) && (currentSpell->GetCastedTime() > 0U))")
probe("no cohort" ai_cpp " && ai::cohort::Applies(sPlayerbotAIConfig.castWaitPreparing, bot->GetGUIDLow()) &&" " &&")
probe("real master held" ai_cpp " &&\n            !HasRealPlayerMaster())" ")")
probe("impure policy" policy "#include <cstdint>" "#include <cstdint>\n#include \"Spells/Spell.h\"")

message(STATUS "cast_wait source contract passed (${probes} negative probes)")
