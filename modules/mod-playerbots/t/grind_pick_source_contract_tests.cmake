cmake_policy(VERSION 3.16)
# twow-repo#601 S2 diagnostic (OB-10 yes + OB-00 18.10.2026): [GrindPick] - every new grind target of GrindTargetValue by
# source (attacker / victim / quest / grind) and grey, per 5-level band, one line per band and minute in ReportBotUpdate.
# Behind AiPlayerbot.BotUpdateTrace, no own key. Pins (OB-10's conditions):
#  - the pure policy GrindPickPolicy.h (12 bands, 4 sources, relaxed atomics, no lock, no game header),
#  - the count only behind the gate (off: nothing but the test - no level, no grey computation, no atomic),
#  - the target choice unchanged: the source is recorded next to the existing returns / assignments, never read by the
#    choice (return statements and result assignments as before, no condition on the recorded source),
#  - the minute line: drained on the switch-on edge with the other recorders, bands without a pick left out.
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
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("aiplayerbot.conf.dist.in" conf)

# ---- 1. Pure policy: 12 bands, 4 sources, atomics, no lock, no game header -------------------------------------
require_text("${policy}" "constexpr std::size_t Bands = 12;" "12 bands L1-5 .. L56-60")
require_text("${policy}" "    Attacker,   // " "source attacker")
require_text("${policy}" "    Victim,     // " "source victim (self-defence counted apart, OB-10)")
require_text("${policy}" "    Quest,      // " "source quest")
require_text("${policy}" "    Grind,      // " "source grind")
require_text("${policy}" "return victimIsBot ? Source::Victim : neededForQuest ? Source::Quest : Source::Grind;" "victim before quest before grind")
require_text("${policy}" "fetch_add(1, std::memory_order_relaxed);" "relaxed atomic counts")
require_text("${policy}" "std::string line = \"[GrindPick] band=L\"" "greppable [GrindPick] line")
foreach(forbidden "mutex" "lock_guard" "#include \"")
  forbid_text("${policy}" "${forbidden}" "lock or project include in the pure policy")
endforeach()

# ---- 2. The count only behind the gate, once per new target ----------------------------------------------------------
region("${grind_cpp}" "Unit* GrindTargetValue::Calculate()" "\nUnit* GrindTargetValue::FindTargetForGrinding" calc_fn)
require_text("${calc_fn}" "if (sPlayerbotAIConfig.botUpdateTrace && target && target->GetObjectGuid() != lastPickTarget)\n    {\n        lastPickTarget = target->GetObjectGuid();\n        ai::grind_pick::Global().Add(ai::grind_pick::BandOf(bot->GetLevel()), lastPickSource, creature && !MaNGOS::XP::Gain(bot, creature));\n    }" "the gated count of a new pick")
require_order("${calc_fn}" "if (sPlayerbotAIConfig.botUpdateTrace && target" "ai::grind_pick::Global().Add(" "the gate before the count")
require_order("${calc_fn}" "ai::grind_pick::Global().Add(" "    return target;\n}" "the count before the unchanged return")
count_text("${grind_cpp}" "grind_pick::Global()" 1 "one count site in GrindTargetValue")
count_text("${grind_cpp}" "    return target;\n" 1 "Calculate returns the target as before")
require_text("${grind_h}" "ai::grind_pick::Source lastPickSource = ai::grind_pick::Source::None;" "per-bot source member")
require_text("${grind_h}" "ObjectGuid lastPickTarget;" "per-bot last counted target")

# ---- 3. The source is recorded, never read by the choice --------------------------------------------------------------
region("${grind_cpp}" "Unit* GrindTargetValue::FindTargetForGrinding(int assistCount)" "\nint GrindTargetValue::GetTargetingPlayerCount" find_fn)
require_order("${find_fn}" "    lastPickSource = ai::grind_pick::Source::None;" "std::list<ObjectGuid> attackers = " "reset before the attacker loop")
require_text("${find_fn}" "        logGrind(unit, \"(hostile) selected.\");\n        lastPickSource = ai::grind_pick::Source::Attacker;\n        return unit;" "attacker loop: source Attacker at its return")
count_text("${find_fn}" "                    result = unit;\n                    resultNeededForQuest = needForQuest;" 1 "group branch keeps the pick's quest need")
count_text("${find_fn}" "                result = unit;\n                resultNeededForQuest = needForQuest;" 1 "solo branch keeps the pick's quest need")
require_text("${find_fn}" "        logGrind(result, \"selected.\");\n        lastPickSource = ai::grind_pick::PickedSource(result->GetVictim() == bot, resultNeededForQuest);" "source of a picked target")
# Unchanged choice: the same returns and result assignments as before the diagnostic, no condition on what it records.
count_text("${find_fn}" "return unit;" 3 "returns of a unit (attacker loop + two vehicle cases, unchanged)")
count_text("${find_fn}" "    return result;\n" 1 "the one result return")
count_text("${find_fn}" "        return NULL;\n" 1 "the empty-targets return")
count_text("${find_fn}" "result = unit;" 2 "result assignments (group / solo, unchanged)")
count_text("${find_fn}" "result = " 3 "no other write to result (its declaration + the two assignments)")
foreach(read_use "if (lastPickSource" "if (resultNeededForQuest" "&& resultNeededForQuest" "|| resultNeededForQuest" "lastPickSource ==" "lastPickSource !=")
  forbid_text("${grind_cpp}" "${read_use}" "the recorded source steering the choice")
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
