# twow-repo#602 ROT-01 (death analysis 21.10.2026, OB-10/OB-00): AiPlayerbot.Rotation.MeleeFleeBreak (default 0, behaviour change)
# keeps "outnumbered" quiet for a melee bot while its flee loop holds, so it fights instead of fleeing on every AI tick.
# Pins: switch default 0 and checked first; the old trigger body unchanged behind it; melee only (ranged keep FleeMultiplier);
# the loop test identical to FleeMultiplier's; panic (X3) untouched; cohort mode; report; pure policy.
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/MeleeFleeBreakPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/strategy/triggers/GenericTriggers.cpp" trig_cpp)
read_file("${PB_SOURCE_DIR}/strategy/triggers/GenericTriggers.h" trig_h)
read_file("${PB_SOURCE_DIR}/strategy/generic/FleeStrategy.cpp" flee_cpp)
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

function(check policy trig_cpp trig_h flee_cpp config_h config_cpp dist mgr_cpp out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho].
  need("${dist}" "\nAiPlayerbot.Rotation.MeleeFleeBreak = 0\n" "dist key, default 0")
  need("${config_h}" "uint32 rotationMeleeFleeBreak = 0;" "config member, default 0")
  need("${config_cpp}" "rotationMeleeFleeBreak = config.GetIntDefault(\"AiPlayerbot.Rotation.MeleeFleeBreak\", 0);\n    if (rotationMeleeFleeBreak > 2)\n        rotationMeleeFleeBreak = 0;" "config read, > 2 = off")
  need("${config_cpp}" "{\"Rotation.MeleeFleeBreak\", Value(rotationMeleeFleeBreak)}," "parsed mode in [ConfigEcho] (owner rule #615)")

  # 2. Pure policy (std + the cohort rule only).
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
      list(APPEND errors "MeleeFleeBreakPolicy.h is not pure: ${inc}")
    endif()
  endforeach()

  # 3. The trigger: switch first, off = the old body unchanged (now OutnumberedActive, X3 inside), on = the break on top.
  between("${trig_cpp}" "bool OutNumberedTrigger::IsActive()" "\n}\n" isactive)
  need("${isactive}" "if (!sPlayerbotAIConfig.rotationMeleeFleeBreak)\n        return OutnumberedActive();\n    return MeleeFleeBreak(OutnumberedActive());" "switch first, then the break on the old result")
  between("${trig_cpp}" "bool OutNumberedTrigger::OutnumberedActive()" "\n}\n" oldbody)
  need("${oldbody}" "if (!sPlayerbotAIConfig.rotationEmergencyLadder)\n        return Outnumbered();" "old body with the X3 part")
  need("${oldbody}" "return !ai::emergency_ladder::EscapeWindowOpen(nowMs, escapeStartMs);" "old body end (X3 escape window)")
  ban("${oldbody}" "rotationMeleeFleeBreak" "the break inside the old body")

  # 4. The break: melee only, cohort mode, the loop test with FleeMultiplier's inputs; a hold returns false.
  between("${trig_cpp}" "bool OutNumberedTrigger::MeleeFleeBreak(bool active)" "\n}\n" brk)
  need("${brk}" "bool const ranged = ai->IsRanged(bot);" "ranged from IsRanged")
  need("${brk}" "mfb::Applies(sPlayerbotAIConfig.rotationMeleeFleeBreak, bot->GetGUIDLow(), ranged,\n            sPlayerbotAIConfig.rotationMeleeFleeBreakRanged)" "cohort mode, ranged only with MeleeFleeBreak.Ranged")
  need("${brk}" "mfb::Holds(int64(time(nullptr)), int64(lastMovement.lastFleeAttempt), lastMovement.fleeCount,\n            int64(sPlayerbotAIConfig.returnDelay / 1000))" "loop test with last movement and ReturnDelay")
  need("${brk}" "mfb::held.fetch_add(1, std::memory_order_relaxed);\n        if (ranged)\n            mfb::heldRanged.fetch_add(1, std::memory_order_relaxed);\n        return false;" "a hold keeps the trigger quiet, ranged part counted")
  need("${trig_h}" "bool MeleeFleeBreak(bool active);" "member function")
  need("${policy}" "return (!ranged || rangedToo) && ai::cohort::Applies(mode, guidLow);" "melee, ranged only with rangedToo, cohort rule")

  # 5. The loop test mirrors FleeMultiplier (FleeStrategy.cpp), whose ranged break stays as it is.
  need("${policy}" "return lastFleeAttempt != 0 && now - lastFleeAttempt <= windowSeconds && fleeCount >= 2;" "loop test")
  need("${flee_cpp}" "if (!lastMovement.lastFleeAttempt || (now - lastMovement.lastFleeAttempt) > window)\n        return 1.0f;" "FleeMultiplier window test")
  need("${flee_cpp}" "if (lastMovement.fleeCount >= 2)\n        return 0.01f;" "FleeMultiplier loop break for ranged")
  need("${flee_cpp}" "if (!ai->IsRanged(bot))\n        return 1.0f;" "FleeMultiplier unchanged for melee")
  ban("${flee_cpp}" "rotationMeleeFleeBreak" "the switch in FleeStrategy")

  # 6. Panic (X3 EmergencyLadder) is not touched.
  between("${trig_cpp}" "bool PanicTrigger::IsActive()" "\n}\n" panic)
  need("${panic}" "if (!sPlayerbotAIConfig.rotationEmergencyLadder)\n        return panic;" "panic body")
  ban("${panic}" "rotationMeleeFleeBreak" "the switch in panic")
  between("${trig_cpp}" "bool PanicTrigger::EmergencyLadder(bool panic)" "\nbool OutNumberedTrigger::IsActive()" ladder)
  ban("${ladder}" "melee_flee_break" "the break in the X3 ladder")

  # 7. Report once per minute after [CastWait], nothing when off.
  before("${mgr_cpp}" "    ReportCastWait();" "    ReportMeleeFleeBreak();" "report after ReportCastWait")
  need("${mgr_cpp}" "if (!sPlayerbotAIConfig.rotationMeleeFleeBreak || now < lastReport + 60)" "report only when on")

  # 8. MeleeFleeBreak.Ranged (step 2, 21.10.): default 0, a bool read, in [ConfigEcho] and in the report; no own cohort (follows the mode).
  need("${dist}" "\nAiPlayerbot.Rotation.MeleeFleeBreak.Ranged = 0\n" "dist key Ranged, default 0")
  need("${config_h}" "bool rotationMeleeFleeBreakRanged = false;" "config member Ranged, default off")
  need("${config_cpp}" "rotationMeleeFleeBreakRanged = config.GetBoolDefault(\"AiPlayerbot.Rotation.MeleeFleeBreak.Ranged\", false);" "config read Ranged")
  need("${config_cpp}" "{\"Rotation.MeleeFleeBreak.Ranged\", Value(rotationMeleeFleeBreakRanged)}," "Ranged in [ConfigEcho]")
  need("${mgr_cpp}" "(unsigned long long)ai::melee_flee_break::heldRanged.exchange(0, std::memory_order_relaxed));" "held_ranged in the report")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${trig_cpp}" "${trig_h}" "${flee_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#602 melee flee break contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#602 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${trig_cpp}" "${trig_h}" "${flee_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#602 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.Rotation.MeleeFleeBreak = 0\n" "\nAiPlayerbot.Rotation.MeleeFleeBreak = 1\n")
probe("unknown mode kept" config_cpp "    if (rotationMeleeFleeBreak > 2)\n        rotationMeleeFleeBreak = 0;" "")
probe("switch not first" trig_cpp "    if (!sPlayerbotAIConfig.rotationMeleeFleeBreak)\n        return OutnumberedActive();\n" "")
probe("ranged included" trig_cpp "bool const ranged = ai->IsRanged(bot);" "bool const ranged = false;")
probe("ranged without its switch" policy "return (!ranged || rangedToo) && ai::cohort::Applies(mode, guidLow);" "return ai::cohort::Applies(mode, guidLow);")
probe("Ranged default on" dist "\nAiPlayerbot.Rotation.MeleeFleeBreak.Ranged = 0\n" "\nAiPlayerbot.Rotation.MeleeFleeBreak.Ranged = 1\n")
probe("Ranged not echoed" config_cpp "{\"Rotation.MeleeFleeBreak.Ranged\", Value(rotationMeleeFleeBreakRanged)}," "")
probe("one flee holds" policy "fleeCount >= 2;" "fleeCount >= 1;")
probe("hold fires" trig_cpp "mfb::heldRanged.fetch_add(1, std::memory_order_relaxed);\n        return false;" "mfb::heldRanged.fetch_add(1, std::memory_order_relaxed);\n        return true;")
probe("FleeMultiplier changed for melee" flee_cpp "if (!ai->IsRanged(bot))\n        return 1.0f;" "if (!ai->IsRanged(bot) && !sPlayerbotAIConfig.rotationMeleeFleeBreak)\n        return 1.0f;")
probe("panic touched" trig_cpp "    if (!sPlayerbotAIConfig.rotationEmergencyLadder)\n        return panic;" "    if (!sPlayerbotAIConfig.rotationEmergencyLadder && !sPlayerbotAIConfig.rotationMeleeFleeBreak)\n        return panic;")
probe("impure policy" policy "#include <cstdint>" "#include <cstdint>\n#include \"Entities/Player.h\"")
probe("report always" mgr_cpp "if (!sPlayerbotAIConfig.rotationMeleeFleeBreak || now < lastReport + 60)" "if (now < lastReport + 60)")

message(STATUS "melee_flee_break source contract passed (${probes} negative probes)")
