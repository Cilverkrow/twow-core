cmake_policy(VERSION 3.16)
# twow-repo#601 S2 diagnostic (OB-10 yes + OB-00 18.10.2026): [GrindPick] - every grind engagement (AttackAnythingAction,
# counted where bot_events logs it - the phase-1 grey metric) by source (attacker / victim / quest / grind) and grey, per
# 5-level band, one line per band and minute in ReportBotUpdate. Behind AiPlayerbot.BotUpdateTrace, no own key.
# Review 18.10.: counted per engagement, not per recalculation of the "grind target" value (recomputed every ~2 s by
# many callers, flipping between candidates). Pins (OB-10's conditions):
#  - the pure policy GrindPickPolicy.h (12 bands, 4 sources, exactly two relaxed fetch_adds, no lock, no string in Add),
#  - the count only behind the gate (off: nothing but the test - no list read, no quest value, no grey computation),
#  - the target choice unchanged: GrindTargetValue carries no diagnostic code at all,
#  - the minute line: drained on the switch-on edge with the other recorders, bands without a count left out.
# Reads only named files below modules/mod-playerbots/src/playerbot (never a glob, never src/modules/Eluna).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 GrindPick: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 GrindPick: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 GrindPick: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_length)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR start "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${start} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#601 GrindPick: ${description}: '${needle}' found ${count} times, expected ${expected}")
  endif()
endfunction()

function(region text start end out)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#601 GrindPick: region start not found: ${start}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#601 GrindPick: region end not found after ${start}: ${end}")
  endif()
  string(SUBSTRING "${tail}" 0 ${b} piece)
  set(${out} "${piece}" PARENT_SCOPE)
endfunction()

read_source("GrindPickPolicy.h" policy)
read_source("strategy/values/GrindTargetValue.cpp" grind_cpp)
read_source("strategy/values/GrindTargetValue.h" grind_h)
read_source("strategy/actions/ChooseTargetActions.cpp" choose)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("aiplayerbot.conf.dist.in" conf)


# ---- 1. Pure policy: 12 bands, 4 sources, relaxed atomics, no lock, no game header ---------------------------------
require_text("${policy}" "constexpr std::size_t Bands = 12;" "12 bands L1-5 .. L56-60")
require_text("${policy}" "    Attacker,   // " "source attacker")
require_text("${policy}" "    Victim,     // " "source victim (self-defence counted apart, OB-10)")
require_text("${policy}" "    Quest,      // " "source quest")
require_text("${policy}" "    Grind,      // " "source grind")
require_text("${policy}" "return attacker ? Source::Attacker : victimIsBot ? Source::Victim : neededForQuest ? Source::Quest : Source::Grind;" "attacker before victim before quest before grind")
count_text("${policy}" "fetch_add(" 2 "exactly the two counts in Add")
count_text("${policy}" "fetch_add(1, std::memory_order_relaxed);" 2 "both relaxed")
region("${policy}" "    void Add(std::size_t band, Source source, bool grey)" "\n    }\n" add_fn)
forbid_text("${add_fn}" "std::string" "a string on the hot path")
require_order("${policy}" "s.grey[b][i] = greys[b][i].exchange(0, std::memory_order_relaxed);" "s.picks[b][i] = picks[b][i].exchange(0, std::memory_order_relaxed);" "greys taken before picks (review)")
require_text("${policy}" "if (s.picks[band][i] || s.grey[band][i])" "a grey-only band is not dropped (review)")
require_text("${policy}" "std::string line = \"[GrindPick] band=L\"" "greppable [GrindPick] line")
foreach(forbidden "mutex" "lock_guard" "#include \"")
  forbid_text("${policy}" "${forbidden}" "lock or project include in the pure policy")
endforeach()

# ---- 2. One count per engagement, where bot_events logs it, only behind the gate -------------------------------------
region("${choose}" "bool ai::AttackAnythingAction::Execute(Event& event)" "\n}\n" execute_fn)
require_order("${execute_fn}" "sPlayerbotAIConfig.logEvent(ai, \"AttackAnythingAction\", grindName" "                if (sPlayerbotAIConfig.botUpdateTrace)\n                {" "counted right after the bot_events line (the phase-1 metric)")
require_order("${execute_fn}" "                if (sPlayerbotAIConfig.botUpdateTrace)\n                {" "ai::grind_pick::Global().Add(" "the gate before every source computation and the count")
require_order("${execute_fn}" "                if (sPlayerbotAIConfig.botUpdateTrace)\n                {" "AI_VALUE(std::list<ObjectGuid>, \"possible attack targets\")" "attacker list read only behind the gate")
require_order("${execute_fn}" "                if (sPlayerbotAIConfig.botUpdateTrace)\n                {" "AI_VALUE2(bool, \"need for quest\"" "quest need read only behind the gate")
require_order("${execute_fn}" "ai::grind_pick::Global().Add(" "if (ai->HasStrategy(\"pull\", BotState::BOT_STATE_COMBAT))" "counted before the pull branch can return")
require_text("${execute_fn}" "ai::grind_pick::EngagedSource(attacker, grindTarget->GetVictim() == bot, neededForQuest)," "the engagement's source")
require_text("${execute_fn}" "creature && !MaNGOS::XP::Gain(bot, creature));" "grey = no XP")
count_text("${choose}" "grind_pick::Global()" 1 "one count site")
count_text("${choose}" "if (sPlayerbotAIConfig.botUpdateTrace)" 1 "one gate")
require_text("${choose}" "#include \"playerbot/GrindPickPolicy.h\"" "include what you use")

# ---- 3. The target choice is untouched: GrindTargetValue has no part in it ------------------------------------------
foreach(untouched "grind_pick" "GrindPick" "lastPick")
  forbid_text("${grind_cpp}" "${untouched}" "a diagnostic hook in the target choice (GrindTargetValue.cpp)")
  forbid_text("${grind_h}" "${untouched}" "a diagnostic member in GrindTargetValue.h")
endforeach()

# ---- 4. Minute line: drained on the switch-on edge, bands without a pick left out ----------------------------------
region("${rnd_mgr}" "void ReportBotUpdate()" "\n}\n" report_fn)
require_order("${report_fn}" "        ai::bot_update::Prevalidate().Take();\n        ai::grind_pick::Global().Take();\n        return;" "    if (now < lastReport + 60)" "drained with the other recorders on the switch-on edge")
require_order("${report_fn}" "    if (now < lastReport + 60)" "    ai::grind_pick::Snapshot const gp = ai::grind_pick::Global().Take();" "minute window taken after the minute check")
require_text("${report_fn}" "    for (std::size_t band = 0; band < ai::grind_pick::Bands; ++band)\n        if (!ai::grind_pick::BandEmpty(gp, band))\n            sLog.outBasic(\"%s\", ai::grind_pick::FormatLine(gp, band).c_str());" "one line per band with picks")
require_text("${rnd_mgr}" "#include \"playerbot/GrindPickPolicy.h\"" "include what you use")
require_text("${conf}" "[GrindPick] line per 5-level band and minute" "documented under BotUpdateTrace")
require_text("${conf}" "\nAiPlayerbot.BotUpdateTrace = 0\n" "BotUpdateTrace default unchanged")

message(STATUS "grind_pick_source_contract: all pins hold")
