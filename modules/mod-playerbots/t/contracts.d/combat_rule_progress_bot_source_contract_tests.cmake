# A3 step 5 P6 (OB-00 08.11.2026 Q1 (a), log only): the combat rule, part ProgressGuard, per-bot line
# "[CombatRule] part=ProgressGuardBot mode= block= guid= cohort= cls= band= combat_ms= noprog_ms= kills= kill_ms= passes= no_ok=
# no_ok_swing=" for the P4/P5 PREREG's bot-cluster bootstrap (the ProgressGuard minute lines are cohort sums). One line per bot and
# 10-minute wall-clock block, and on a band change; written by the bot's first reading after it. No new key: the line follows
# AiPlayerbot.CombatRule.ProgressGuard (its callers run only with the switch on and never for a real-player-master bot).
# Pins: the pure totals (600 s blocks, an empty block is never written, a later block or a band change writes first, kill / pass
# fields); the glue (write before add, the line format, the totals cleared after a write); the two hooks right after the cohort
# counters in ObserveCombatProgress / ActOnCombatProgress; the member; the .dist text.
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
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

function(need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(errors "${errors};missing ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(errors "${errors};order ${what}: ${first} before ${second}" PARENT_SCOPE)
  endif()
endfunction()

function(check policy glue glue_h ai_cpp ai_h dist out_var)
  set(errors "")

  # 1. The pure totals.
  need("${policy}" "constexpr std::uint64_t ProgressBotBlockSeconds = 600;" "10-minute blocks")
  need("${policy}" "    return unixSeconds - unixSeconds % ProgressBotBlockSeconds;" "block = start of the wall-clock block")
  need("${policy}" "    return !t.combatMs && !t.kills && !t.passes;" "empty = no combat time, kill or pass")
  need("${policy}" "    return !ProgressBotEmpty(t) && (t.block != block || t.band != band);" "write on a later block or a band change, never empty")
  need("${policy}" "    if (!s.combatMs && !s.kill)\n        return;" "a reading without combat time or kill adds nothing")
  need("${policy}" "    t.noProgressMs += s.noProgressMs;\n    if (s.kill)\n    {\n        ++t.kills;\n        t.killMs += s.killMs;" "kill and time-to-kill")
  need("${policy}" "    ++t.passes;\n    if (noOk)\n    {\n        ++t.noOk;\n        if (swing)\n            ++t.noOkSwing;" "passes, no OK, swing")

  # 2. The glue: write the open block first, then add; the line format; the totals cleared after a write.
  need("${glue_h}" "void CountProgressBot(Player* bot, ProgressBotTotals& totals, ProgressStep const& step);" "step declaration")
  need("${glue_h}" "void CountCombatPassBot(Player* bot, ProgressBotTotals& totals, bool noOk, bool swing);" "pass declaration")
  need("${glue}" "    if (ProgressBotFlushDue(totals, block, band))\n        WriteProgressBot(bot, totals);\n    AddProgressBotStep(totals, block, band, step);" "step: write first, then add")
  need("${glue}" "    if (ProgressBotFlushDue(totals, block, band))\n        WriteProgressBot(bot, totals);\n    AddProgressBotPass(totals, block, band, noOk, swing);" "pass: write first, then add")
  need("${glue}" "    std::uint64_t const block = ProgressBotBlock(std::uint64_t(std::time(nullptr)));\n    unsigned const band = ProgressBand(bot->GetLevel());" "wall-clock block and the bot's band")
  need("${glue}" "\"[CombatRule] part=ProgressGuardBot mode=%u block=%llu guid=%u cohort=%c cls=%u band=%u combat_ms=%llu \"\n        \"noprog_ms=%llu kills=%llu kill_ms=%llu passes=%llu no_ok=%llu no_ok_swing=%llu\"" "line format")
  need("${glue}" "    t = ProgressBotTotals();\n}" "totals cleared after a write")

  # 3. The hooks right after the cohort counters (same gate: switch on, no real player master), and the member.
  need("${ai_cpp}" "    ai::combat_rule::CountProgress(bot, step);\n    ai::combat_rule::CountProgressBot(bot, progressBotTotals, step);" "step hook after CountProgress")
  need("${ai_cpp}" "    ai::combat_rule::CountCombatPass(bot, combatIdleTick, swing);\n    ai::combat_rule::CountCombatPassBot(bot, progressBotTotals, combatIdleTick, swing);" "pass hook after CountCombatPass")
  need("${ai_cpp}" "        if (sPlayerbotAIConfig.combatRuleProgressGuard && !HasRealPlayerMaster())\n            ObserveCombatProgress();" "reading only with the switch on")
  need("${ai_h}" "    ai::combat_rule::ProgressBotTotals progressBotTotals;" "member")

  # 4. .dist names the line, before the key.
  before("${dist}" "# Per bot (P6, log only, for the PREREG's bot-cluster bootstrap): \"[CombatRule] part=ProgressGuardBot" "\nAiPlayerbot.CombatRule.ProgressGuard = 0\n" ".dist text")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${glue}" "${glue_h}" "${ai_cpp}" "${ai_h}" "${dist}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "combat_rule_progress_bot contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "ProgressGuardBot probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${glue}" "${glue_h}" "${ai_cpp}" "${ai_h}" "${dist}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "ProgressGuardBot probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("hour blocks" policy "ProgressBotBlockSeconds = 600;" "ProgressBotBlockSeconds = 3600;")
probe("empty written" policy "    return !ProgressBotEmpty(t) && (t.block != block || t.band != band);" "    return t.block != block || t.band != band;")
probe("band change ignored" policy "(t.block != block || t.band != band)" "(t.block != block)")
probe("swing without no-ok" policy "        ++t.noOk;\n        if (swing)\n            ++t.noOkSwing;" "        ++t.noOk;\n    }\n    if (swing)\n    {\n        ++t.noOkSwing;")
probe("add before write" glue "    if (ProgressBotFlushDue(totals, block, band))\n        WriteProgressBot(bot, totals);\n    AddProgressBotStep(totals, block, band, step);" "    AddProgressBotStep(totals, block, band, step);\n    if (ProgressBotFlushDue(totals, block, band))\n        WriteProgressBot(bot, totals);")
probe("totals kept" glue "    t = ProgressBotTotals();\n}" "}")
probe("cohort field gone" glue " guid=%u cohort=%c cls=%u" " guid=%u cls=%u")
probe("pass hook missing" ai_cpp "    ai::combat_rule::CountCombatPassBot(bot, progressBotTotals, combatIdleTick, swing);" "")
probe("step hook missing" ai_cpp "    ai::combat_rule::CountProgressBot(bot, progressBotTotals, step);" "")
probe("dist text gone" dist "# Per bot (P6, log only, for the PREREG's bot-cluster bootstrap)" "# Per bot")

message(STATUS "combat_rule_progress_bot source contract passed (${probes} negative probes)")
