# twow-repo#602 ROT-01 I-1 (owner decision relayed by OB-00 25.11.2026 "Nahkampf ja"; OB-00 decision (a) 25.11.2026):
# AiPlayerbot.Rotation.PriestOomMelee (default 0, behaviour change) arms OomBackoff's melee fallback alone, for priests only.
# Pins: switch default 0 and unknown = off, parsed mode in [ConfigEcho] right after Rotation.OomBackoff; the pure rule (priest
# class, cohort mode, either switch arms, counter attribution); the three gates that took the OomBackoff mode before (the
# ranged node, OomMeleeFallback, the "enemy too close for spell" early-out) take the new key; the backoff does not (no
# PriestOomMelee in PlayerbotAI.cpp / .h); the [PriestOomMelee] report once a minute while on, zeros included.
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/OomBackoffPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/strategy/triggers/RangeTriggers.h" range_h)
read_file("${PB_SOURCE_DIR}/strategy/generic/RangedCombatStrategy.cpp" ranged_cpp)
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

function(check policy range_h ranged_cpp ai_cpp ai_h config_h config_cpp dist mgr_cpp out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho], sorted.
  need("${dist}" "\nAiPlayerbot.Rotation.PriestOomMelee = 0\n" "dist key, default 0")
  need("${config_h}" "uint32 rotationPriestOomMelee = 0;" "config member, default 0")
  need("${config_cpp}" "rotationPriestOomMelee = config.GetIntDefault(\"AiPlayerbot.Rotation.PriestOomMelee\", 0);   // twow-repo#602 ROT-01 I-1\n    if (rotationPriestOomMelee > 3)\n        rotationPriestOomMelee = 0;" "config read, > 3 = off (mode 3 = cohort A, crossover)")
  need("${config_cpp}" "            {\"Rotation.OomBackoff\", Value(rotationOomBackoff)},\n            {\"Rotation.PriestOomMelee\", Value(rotationPriestOomMelee)},\n            {\"Rotation.ReachDropEngagement\"," "parsed mode in [ConfigEcho], sorted after Rotation.OomBackoff")

  # 2. Pure rule: priest class only, the cohort mode, either switch arms the fallback, counters by the arming switch.
  need("${policy}" "inline bool PriestMeleeApplies(std::uint32_t priestMode, std::uint32_t cls, std::uint32_t guidLow)\n{\n    return cls == ClassPriest && Applies(priestMode, guidLow);\n}" "priests only, cohort mode")
  need("${policy}" "return Applies(oomMode, guidLow) || PriestMeleeApplies(priestMode, cls, guidLow);" "either switch arms the fallback")
  need("${policy}" "return !Applies(oomMode, guidLow) && PriestMeleeApplies(priestMode, cls, guidLow);" "counter attribution")
  need("${policy}" "inline std::atomic<std::uint64_t> priestFallback[2]{ { 0 }, { 0 } };" "fallback counters per cohort")
  need("${policy}" "inline std::atomic<std::uint64_t> priestStayed[2]{ { 0 }, { 0 } };" "stayed counters per cohort")
  need("${policy}" "return ai::cohort::InB(guidLow) ? 1u : 0u;" "cohort index 0 = A, 1 = B")

  # 3. The three gates that took the OomBackoff mode take the new key too (else an inert key or a flee / melee ping-pong).
  need("${ranged_cpp}" "if (ai::oom_backoff::MeleeArmed(sPlayerbotAIConfig.rotationOomBackoff, sPlayerbotAIConfig.rotationPriestOomMelee,\n            ai->GetBot()->getClass(), ai->GetBot()->GetGUIDLow()))\n        triggers.push_back(new TriggerNode(\n            \"oom melee fallback\"," "gate 1: the ranged node")
  between("${range_h}" "inline bool OomMeleeFallback(PlayerbotAI* ai, AiObjectContext* context)" "\n    }\n" fb)
  need("${fb}" "if (!ai::oom_backoff::MeleeArmed(sPlayerbotAIConfig.rotationOomBackoff, sPlayerbotAIConfig.rotationPriestOomMelee,\n                bot->getClass(), bot->GetGUIDLow()))\n            return false;" "gate 2: OomMeleeFallback")
  need("${fb}" "return ai::oom_backoff::MeleeFallback(true, bot->getClass()," "the old fallback rule (class, mana mark, no wand) stays")
  between("${range_h}" "class OomMeleeFallbackTrigger : public Trigger" "    };\n" fbt)
  need("${fbt}" "if (ai::oom_backoff::ViaPriestKey(sPlayerbotAIConfig.rotationOomBackoff, sPlayerbotAIConfig.rotationPriestOomMelee,\n                    bot->getClass(), bot->GetGUIDLow()))\n                ai::oom_backoff::priestFallback[ai::oom_backoff::CohortIndex(bot->GetGUIDLow())].fetch_add(1, std::memory_order_relaxed);\n            else\n                ai::oom_backoff::fallback.fetch_add(1, std::memory_order_relaxed);" "fallback counted by the arming switch and cohort")
  between("${range_h}" "class EnemyTooCloseForSpellTrigger : public Trigger" "class EnemyTooCloseForShootTrigger" tooclose)
  need("${tooclose}" "if (!sPlayerbotAIConfig.rotationOomBackoff && !sPlayerbotAIConfig.rotationPriestOomMelee)\n                return TooClose();" "gate 3: too-close early-out")
  need("${tooclose}" "ai::oom_backoff::priestStayed[ai::oom_backoff::CohortIndex(bot->GetGUIDLow())].fetch_add(1, std::memory_order_relaxed);" "stayed counted for the key and cohort")

  # 4. No backoff from this key: the backoff stays on Rotation.OomBackoff alone.
  ban("${ai_cpp}" "PriestOomMelee" "the key in PlayerbotAI.cpp (backoff)")
  ban("${ai_h}" "PriestOomMelee" "the key in PlayerbotAI.h")
  need("${ai_cpp}" "        if (sPlayerbotAIConfig.rotationOomBackoff && !HasRealPlayerMaster())\n            BackoffOutOfMana(yieldSetDelay, min);" "backoff only with OomBackoff")

  # 5. Report once a minute while on (zeros included), after [OomBackoff].
  between("${mgr_cpp}" "void ReportPriestOomMelee()" "\n}\n" rep)
  need("${rep}" "if (!sPlayerbotAIConfig.rotationPriestOomMelee || now < lastReport + 60)\n        return;" "report only when on")
  need("${rep}" "sLog.outBasic(\"[PriestOomMelee] mode=%u oombackoff=%u fallback_a=%llu fallback_b=%llu stayed_a=%llu stayed_b=%llu\"," "report line, per cohort (control null)")
  need("${rep}" "ai::oom_backoff::priestFallback[0].exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::oom_backoff::priestFallback[1].exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::oom_backoff::priestStayed[0].exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::oom_backoff::priestStayed[1].exchange(0, std::memory_order_relaxed));" "report drains its own counters in the line's order a, b")
  ban("${rep}" "ai::oom_backoff::fallback." "report drains the OomBackoff counters")
  before("${mgr_cpp}" "    ReportOomBackoff();" "    ReportPriestOomMelee();" "report after ReportOomBackoff")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${range_h}" "${ranged_cpp}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#602 I-1 priest oom melee contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#602 I-1 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${range_h}" "${ranged_cpp}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#602 I-1 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.Rotation.PriestOomMelee = 0\n" "\nAiPlayerbot.Rotation.PriestOomMelee = 1\n")
probe("member on" config_h "uint32 rotationPriestOomMelee = 0;" "uint32 rotationPriestOomMelee = 1;")
probe("unknown mode kept" config_cpp "    if (rotationPriestOomMelee > 3)\n        rotationPriestOomMelee = 0;" "")
probe("mode 3 clamped" config_cpp "    if (rotationPriestOomMelee > 3)\n" "    if (rotationPriestOomMelee > 2)\n")
probe("no echo" config_cpp "            {\"Rotation.PriestOomMelee\", Value(rotationPriestOomMelee)},\n" "")
probe("mage included" policy "return cls == ClassPriest && Applies(priestMode, guidLow);" "return WandClass(cls) && Applies(priestMode, guidLow);")
probe("no cohort mode" policy "return cls == ClassPriest && Applies(priestMode, guidLow);" "return cls == ClassPriest && priestMode != 0;")
probe("node ignores the key" ranged_cpp "MeleeArmed(sPlayerbotAIConfig.rotationOomBackoff, sPlayerbotAIConfig.rotationPriestOomMelee,\n            ai->GetBot()->getClass()" "MeleeArmed(sPlayerbotAIConfig.rotationOomBackoff, 0,\n            ai->GetBot()->getClass()")
probe("fallback ignores the key" range_h "MeleeArmed(sPlayerbotAIConfig.rotationOomBackoff, sPlayerbotAIConfig.rotationPriestOomMelee,\n                bot->getClass(), bot->GetGUIDLow()))\n            return false;" "MeleeArmed(sPlayerbotAIConfig.rotationOomBackoff, 0,\n                bot->getClass(), bot->GetGUIDLow()))\n            return false;")
probe("too close ignores the key" range_h "if (!sPlayerbotAIConfig.rotationOomBackoff && !sPlayerbotAIConfig.rotationPriestOomMelee)\n                return TooClose();" "if (!sPlayerbotAIConfig.rotationOomBackoff)\n                return TooClose();")
probe("backoff leak" ai_cpp "        if (sPlayerbotAIConfig.rotationOomBackoff && !HasRealPlayerMaster())" "        if ((sPlayerbotAIConfig.rotationOomBackoff || sPlayerbotAIConfig.rotationPriestOomMelee) && !HasRealPlayerMaster())")
probe("report always" mgr_cpp "if (!sPlayerbotAIConfig.rotationPriestOomMelee || now < lastReport + 60)" "if (now < lastReport + 60)")
probe("report not called" mgr_cpp "    ReportPriestOomMelee();   //" "    //")
probe("cohorts swapped in the line" mgr_cpp "priestFallback[0].exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::oom_backoff::priestFallback[1]" "priestFallback[1].exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::oom_backoff::priestFallback[0]")
probe("cohort index inverted" policy "return ai::cohort::InB(guidLow) ? 1u : 0u;" "return ai::cohort::InB(guidLow) ? 0u : 1u;")
probe("shared counters" policy "return !Applies(oomMode, guidLow) && PriestMeleeApplies(priestMode, cls, guidLow);" "return false;")

message(STATUS "priest_oom_melee source contract passed (${probes} negative probes)")
