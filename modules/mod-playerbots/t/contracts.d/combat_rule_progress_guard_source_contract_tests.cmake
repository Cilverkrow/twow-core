# A3 step 5 (ADR-0041; OB-00 08.11.2026, design A3-S5-PROGRESSGUARD-ENTWURF.md; A3 §4.3 R8): the combat rule, part ProgressGuard
# (AiPlayerbot.CombatRule.ProgressGuard, default 0; 1 all, 2 cohort B, 3 cohort A; DeadlineSeconds 15, BreakSeconds 45, BackoffMs
# 1000, IgnoreSeconds 120). In combat, progress is only damage to the target or a shrinking distance to it; an OK action does not
# count; the clock runs per combat episode and does not restart on a target switch. Without progress: backoff wait, then a break
# (target on the hotfix 8.2 ignore list, dropped as current target; with the supervisor on, one Event{CombatLoop} for its CombatGuard
# frame, taken with PlayerbotAI::TakeCombatLoopEvent). The guard never moves the bot.
# Pins: keys and clamps, [ConfigEcho] sorted, .dist; the pure clock (progress = health drop or 1 yd approach, no OK input, no restart
# on a switch, regeneration no progress, kill, window / long loop, break every BreakMs, wrap-safe); the glue (reading before the pass,
# both cohorts while on, nothing read while off; acts only for the cohort, not for heal strategies or a real player master, not while
# casting or below LowHealth, never shortens another delay; wake on switch / damage / low health); the break (ignore list, current
# target dropped, no movement call, event only with the supervisor on, no mailbox call); the minute lines only with the switch on.
# Every check runs once on the real sources and once per negative probe.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/CombatRulePolicy.h" policy)
read_file("${PB_SOURCE_DIR}/CombatRule.cpp" glue)
read_file("${PB_SOURCE_DIR}/CombatRule.h" glue_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

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
# The text between the first "first" and the next "last" after it ("" when not found).
function(region text first last out_var)
  set(${out_var} "" PARENT_SCOPE)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${last}" b)
  if(b EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} cut)
  set(${out_var} "${cut}" PARENT_SCOPE)
endfunction()

function(check policy glue glue_h ai_cpp ai_h config_h config_cpp dist out_var)
  set(errors "")

  # 1. Keys: mode default 0 and > 3 off; clamps; [ConfigEcho] sorted after PriestOverlay; .dist.
  need("${dist}" "\nAiPlayerbot.CombatRule.ProgressGuard = 0\n" "dist key, default 0")
  need("${dist}" "\nAiPlayerbot.CombatRule.ProgressDeadlineSeconds = 15\n" "dist DeadlineSeconds 15")
  need("${dist}" "\nAiPlayerbot.CombatRule.ProgressBreakSeconds = 45\n" "dist BreakSeconds 45")
  need("${dist}" "\nAiPlayerbot.CombatRule.ProgressBackoffMs = 1000\n" "dist BackoffMs 1000")
  need("${dist}" "\nAiPlayerbot.CombatRule.ProgressIgnoreSeconds = 120\n" "dist IgnoreSeconds 120")
  need("${config_h}" "    uint32 combatRuleProgressGuard = 0;\n" "config member, default 0")
  need("${config_cpp}" "    combatRuleProgressGuard = uint32(config.GetIntDefault(\"AiPlayerbot.CombatRule.ProgressGuard\", 0));\n    if (combatRuleProgressGuard > 3)\n        combatRuleProgressGuard = 0;\n" "config read, > 3 = off")
  need("${config_cpp}" "ClampProgressBreak(config.GetIntDefault(\"AiPlayerbot.CombatRule.ProgressBreakSeconds\", 45),\n        combatRuleProgressDeadlineSeconds);" "break not below the deadline")
  need("${config_cpp}" "            {\"CombatRule.PriestOverlay\", Value(combatRulePriestOverlay)},\n            {\"CombatRule.ProgressBackoffMs\", Value(combatRuleProgressBackoffMs)},\n            {\"CombatRule.ProgressBreakSeconds\", Value(combatRuleProgressBreakSeconds)},\n            {\"CombatRule.ProgressDeadlineSeconds\", Value(combatRuleProgressDeadlineSeconds)},\n            {\"CombatRule.ProgressGuard\", Value(combatRuleProgressGuard)},\n            {\"CombatRule.ProgressIgnoreSeconds\", Value(combatRuleProgressIgnoreSeconds)},\n" "in [ConfigEcho], sorted after CombatRule.PriestOverlay (the key after it is not pinned)")

  # 2. The pure clock: no game types; progress = health drop or approach by gainYards; OK is no input; switch keeps the clock.
  region("${policy}" "inline ProgressStep ObserveProgress(" "\n}\n" clock)
  if(clock STREQUAL "")
    set(errors "${errors};missing the clock ObserveProgress")
  endif()
  need("${clock}" "            if (r.targetHealth < c.targetHealth)\n                progress = true;" "damage = progress")
  need("${clock}" "            if (r.distance + l.gainYards <= c.bestDistance)\n            {\n                progress = true;\n                c.bestDistance = r.distance;\n            }" "approach by gainYards below the best distance = progress")
  need("${clock}" "            c.targetHealth = r.targetHealth;   // regeneration raises the baseline; the next drop counts again" "regeneration is no progress")
  need("${clock}" "        // A target switch is no progress and does not restart the clock; the old target's death is a kill." "switch keeps the clock")
  ban("${clock}" "c.lastProgressMs = r.nowMs;\n        EngageTarget" "a switch restarting the clock")
  need("${clock}" "    if (gap >= l.breakMs && (!c.broken || r.nowMs - c.lastBreakMs >= l.breakMs))" "break at BreakMs, again every BreakMs")
  need("${clock}" "    s.verdict = gap >= l.deadlineMs ? ProgressVerdict::Backoff : ProgressVerdict::Quiet;" "backoff from the deadline")
  need("${policy}" "    std::uint32_t const gap = r.nowMs - c.lastProgressMs;" "wrap-safe gap")
  ban("${policy}" "#include \"playerbot/" "game includes in the pure policy")
  need("${policy}" "inline bool ProgressShouldWake(ProgressClock const& c, std::uint64_t target, std::uint32_t targetHealth, std::uint32_t healthPct,\n    std::uint32_t lowHealth)\n{\n    return target != c.target || targetHealth < c.targetHealth || healthPct < lowHealth;\n}" "wake on switch, damage, low health")

  # 3. The reading: before the pass (before the supervisor gate), both cohorts while on, nothing read while off.
  need("${ai_cpp}" "        progressBackoffPending = false;\n        if (sPlayerbotAIConfig.combatRuleProgressGuard && !HasRealPlayerMaster())\n            ObserveCombatProgress();\n        else\n        {\n            progressVerdict = ai::combat_rule::ProgressVerdict::Off;\n            progressClock = ai::combat_rule::ProgressClock();\n        }\n\n        // A3 step 3 (ADR-0041, AiPlayerbot.Supervisor, default 0)" "reading before the supervisor gate, off = nothing read")
  region("${ai_cpp}" "void PlayerbotAI::ObserveCombatProgress()" "\n}\n" observe)
  need("${observe}" "    reading.inCombat = currentEngine == engines[(uint8)BotState::BOT_STATE_COMBAT];" "combat = the combat engine runs")
  need("${observe}" "            reading.distance = bot->GetDistance(target, false, DIST_CALC_COMBAT_REACH);" "combat-reach distance")
  ban("${observe}" "cohort::Applies" "a cohort filter on the reading (both cohorts are measured)")
  ban("${observe}" "combatIdleTick" "OK results as progress")

  # 4. The acts: after the pass, cohort only, no heal strategy, not while casting / below LowHealth, never shorter than another delay.
  need("${ai_cpp}" "            BackoffOutOfMana(yieldSetDelay, min);\n\n        // A3 step 5 (AiPlayerbot.CombatRule.ProgressGuard, default 0): counts the combat pass and, for the switch's cohort, acts on\n        // this pass's progress reading (backoff wait, break).\n        if (sPlayerbotAIConfig.combatRuleProgressGuard && !HasRealPlayerMaster())\n            ActOnCombatProgress();" "acts after the OOM backoff")
  region("${ai_cpp}" "void PlayerbotAI::ActOnCombatProgress()" "\n}\n" act)
  need("${act}" "    if (!ai::cohort::Applies(sPlayerbotAIConfig.combatRuleProgressGuard, bot->GetGUIDLow()) || ContainsStrategy(STRATEGY_TYPE_HEAL))\n        return;" "acts only for the cohort, not for healers")
  need("${act}" "    if (!backoffMs || bot->IsNonMeleeSpellCasted(true, false, true) || uint32(bot->GetHealthPercent()) < sPlayerbotAIConfig.lowHealth ||\n        aiInternalUpdateDelay >= backoffMs)\n        return;" "no backoff while casting, below LowHealth, or over a longer delay")
  need("${act}" "    ai::combat_rule::CountCombatPass(bot, combatIdleTick, swing);" "combat passes counted (both cohorts)")
  need("${ai_cpp}" "    if (progressBackoffPending)\n    {\n        Unit* const target = aiObjectContext->GetValue<Unit*>(\"current target\")->Get();\n        if (ai::combat_rule::ProgressShouldWake(" "early wake")

  # 5. The break: ignore list + current target dropped; no movement; the event only with the supervisor on; no mailbox call.
  region("${ai_cpp}" "void PlayerbotAI::BreakCombatLoop()" "\n}\n" brk)
  need("${brk}" "        unreachableTargets.Add(progressClock.target, uint32(time(nullptr)), sPlayerbotAIConfig.combatRuleProgressIgnoreSeconds);" "target on the ignore list")
  need("${brk}" "            aiObjectContext->GetValue<Unit*>(\"current target\")->Set(nullptr);" "current target dropped")
  need("${brk}" "    if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, bot->GetGUIDLow()))\n    {\n        combatLoopEvent = ai::combat_rule::MakeCombatLoopEvent(" "event only with the supervisor on")
  foreach(word "MotionMaster" "MoveTo" "MovePoint" "TeleportTo" "AttackStop" "Post(" "DoSpecificAction")
    ban("${brk}" "${word}" "the break moving the bot or calling the mailbox")
  endforeach()
  need("${glue}" "    event.kind = ai::supervisor::EventKind::CombatLoop;" "event kind CombatLoop")
  need("${glue}" "    event.source = std::uint8_t(ai::supervisor::Requester::CombatGuard);" "event source CombatGuard")
  need("${ai_h}" "    bool TakeCombatLoopEvent(ai::supervisor::Event& out)\n    {\n        if (!combatLoopPending)\n            return false;\n        out = combatLoopEvent;\n        combatLoopPending = false;\n        return true;\n    }" "the frame's interface: take the event once")

  # 6. The minute lines only with the switch on; reported with the other parts.
  need("${glue}" "    if (!sPlayerbotAIConfig.combatRuleProgressGuard || now < lastReport + 60)\n        return;" "minute line only when on")
  need("${glue}" "    ReportRestBeforePull();\n    ReportProgressGuard();\n" "reported with the other parts")
  need("${glue}" "\"[CombatRule] part=ProgressGuard mode=%u deadline_s=%u break_s=%u backoff_ms=%u ignore_s=%u episodes=%llu/%llu \"" "line format")
  need("${glue}" "\"[CombatRule] part=ProgressGuardCls mode=%u band=%u cls=%s\"" "per band x class line")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${glue}" "${glue_h}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "A3 step 5 combat rule ProgressGuard contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "ProgressGuard probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${glue}" "${glue_h}" "${ai_cpp}" "${ai_h}" "${config_h}" "${config_cpp}" "${dist}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "ProgressGuard probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.CombatRule.ProgressGuard = 0\n" "\nAiPlayerbot.CombatRule.ProgressGuard = 1\n")
probe("unknown mode kept" config_cpp "    if (combatRuleProgressGuard > 3)\n        combatRuleProgressGuard = 0;\n" "")
probe("echo missing" config_cpp "            {\"CombatRule.ProgressGuard\", Value(combatRuleProgressGuard)},\n" "")
probe("approach any step" policy "            if (r.distance + l.gainYards <= c.bestDistance)" "            if (r.distance < c.bestDistance)")
probe("regeneration counts" policy "            if (r.targetHealth < c.targetHealth)\n" "            if (r.targetHealth != c.targetHealth)\n")
probe("switch restarts" policy "        EngageTarget(c, r);\n    }\n    else if (r.target)" "        c.lastProgressMs = r.nowMs;\n        EngageTarget(c, r);\n    }\n    else if (r.target)")
probe("break once only" policy "(!c.broken || r.nowMs - c.lastBreakMs >= l.breakMs)" "(!c.broken)")
probe("OK as progress" ai_cpp "    reading.inCombat = currentEngine == engines[(uint8)BotState::BOT_STATE_COMBAT];" "    reading.inCombat = currentEngine == engines[(uint8)BotState::BOT_STATE_COMBAT] && !combatIdleTick;")
probe("reading only in the cohort" ai_cpp "    reading.nowMs = WorldTimer::getMSTime();\n" "    reading.nowMs = WorldTimer::getMSTime();\n    if (!ai::cohort::Applies(1, 0)) return;\n")
probe("reads while off" ai_cpp "        if (sPlayerbotAIConfig.combatRuleProgressGuard && !HasRealPlayerMaster())\n            ObserveCombatProgress();" "        if (!HasRealPlayerMaster())\n            ObserveCombatProgress();")
probe("healers act" ai_cpp " || ContainsStrategy(STRATEGY_TYPE_HEAL))\n        return;\n\n    if (progressVerdict == ai::combat_rule::ProgressVerdict::Break)" ")\n        return;\n\n    if (progressVerdict == ai::combat_rule::ProgressVerdict::Break)")
probe("backoff while casting" ai_cpp "    if (!backoffMs || bot->IsNonMeleeSpellCasted(true, false, true) || " "    if (!backoffMs || ")
probe("shortens another delay" ai_cpp " ||\n        aiInternalUpdateDelay >= backoffMs)\n        return;" ")\n        return;")
probe("break moves" ai_cpp "    ai::combat_rule::CountProgressAct(bot, ai::combat_rule::ProgressAct::Break);\n" "    ai::combat_rule::CountProgressAct(bot, ai::combat_rule::ProgressAct::Break);\n    bot->GetMotionMaster()->MovePoint(0, 0, 0, 0);\n")
probe("event without supervisor" ai_cpp "    if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, bot->GetGUIDLow()))\n    {\n        combatLoopEvent" "    {\n        combatLoopEvent")
probe("no ignore" ai_cpp "        unreachableTargets.Add(progressClock.target, uint32(time(nullptr)), sPlayerbotAIConfig.combatRuleProgressIgnoreSeconds);\n" "")
probe("minute line always on" glue "    if (!sPlayerbotAIConfig.combatRuleProgressGuard || now < lastReport + 60)" "    if (now < lastReport + 60)")

message(STATUS "combat_rule_progress_guard source contract passed (${probes} negative probes)")
