# twow-repo#333 Berufe P1-ae (OB-00 06.11.2026 after M1r run 10: a skinner's tool gate said turn_in for 49 min while the bot was
# in the 8.32 grind fallback since 10:35 - no quest request, so no turn-in and no no-route mark). AiPlayerbot.ProfessionTraining.
# TurnInGateSkipGrindFallback (code default 0; 1 all, 2 cohort B, 3 cohort A): in the grind fallback a finished quest does not hold
# the profession trips. Proof line "[ProfTravel] state=turnin_grind_fallback" (hourly per bot).
# Deletion target (ADR-0041 rule 4): goes with A3 step 3 (the supervisor owns the travel target and the mode).
# This contract pins the switch (dist key, member, read with > 3 = off, sorted [ConfigEcho] entry), the pure rule and its tests
# (t/profession_travel_policy_tests.cpp), the state's inputs and the gate's order.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-ae TurnInGateSkipGrindFallback: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-ae TurnInGateSkipGrindFallback: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 P1-ae TurnInGateSkipGrindFallback: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 P1-ae TurnInGateSkipGrindFallback: region end not found after '${first}': '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#333 P1-ae TurnInGateSkipGrindFallback: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/ProfessionTravelPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" request)
read_source("${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create)
read_source("${PB_MODULE_DIR}/t/profession_travel_policy_tests.cpp" tests)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented with its proof line and deletion target.
require_text("${dist}" "\nAiPlayerbot.ProfessionTraining.TurnInGateSkipGrindFallback = 0\n" "dist key, default 0")
require_text("${dist}" "[ProfTravel] state=turnin_grind_fallback bot= level= quests= failures=" "dist: the proof line")
require_text("${dist}" "Deletion target (ADR-0041 rule 4): goes with A3 step 3." "dist: deletion target")
require_text("${config_h}" "uint32 professionTrainingTurnInGateSkipGrindFallback = 0;" "config member, default 0")
require_text("${config_cpp}" "professionTrainingTurnInGateSkipGrindFallback = config.GetIntDefault(\"AiPlayerbot.ProfessionTraining.TurnInGateSkipGrindFallback\", 0);\n    if (professionTrainingTurnInGateSkipGrindFallback > 3)\n    {\n        sLog.outError(\"[ProfTravel] config key=TurnInGateSkipGrindFallback value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", professionTrainingTurnInGateSkipGrindFallback);\n        professionTrainingTurnInGateSkipGrindFallback = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "            {\"ProfessionTraining.TripNoGuildTrip\", Value(professionTrainingTripNoGuildTrip)},\n            {\"ProfessionTraining.TurnInGateSkipGrindFallback\", Value(professionTrainingTurnInGateSkipGrindFallback)},\n            {\"ProfessionTraining.TurnInGateSkipNoRoute\", Value(professionTrainingTurnInGateSkipNoRoute)}," "parsed mode in [ConfigEcho], sorted")

# 1. The pure rule: the 8.32 grind fallback (minFailures 0 = off) without the other grind modes and without the S1 turn-in yield.
between("${policy}" "inline bool InGrindFallback(bool questFirst, bool otherGrindMode, bool s1TurnInYield, std::uint32_t routeFailures, std::uint32_t minFailures)\n{" "\n}\n" rule)
require_text("${rule}" "    return questFirst && !otherGrindMode && !s1TurnInYield && minFailures && routeFailures >= minFailures;" "(1) the rule")
require_text("${tests}" "InGrindFallback(true, false, false, 2, 2)" "(1) the run-10 case in the policy tests")
require_text("${tests}" "InGrindFallback(true, false, true, 9, 2)" "(1) the S1 case in the policy tests")

# 2. The state: switch and cohort first (off: nothing read), a real master excluded, the same inputs as the grind fallback.
between("${travel_cpp}" "bool ProfessionTripGrindFallback(Player* bot)\n{" "\n}\n" state)
require_order("${state}" "if (!ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTurnInGateSkipGrindFallback, bot->GetGUIDLow()))\n        return false;" "AI_VALUE2(" "(2) the switch before any value read")
require_text("${state}" "if (!ai || ai->HasRealPlayerMaster())\n        return false;" "(2) no real master")
require_text("${state}" "ai->IsQuestNoGrindTrip() || ai->IsQuestGapGrind()," "(2) the other grind modes keep their rules")
require_text("${state}" "sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow())," "(2) S1 keeps its turn-in yield")
require_text("${state}" "uint32(std::max(0, AI_VALUE2(int, \"manual int\", \"quest route failures\"))),\n        sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures);" "(2) the 8.32 counter and threshold")
require_text("${request}" "[QuestSearch] state=grind_fallback bot=%u level=%u failures=%d distance=%.0f" "(2) the fallback line it mirrors")
count_text("${travel_cpp}" "professionTrainingTurnInGateSkipGrindFallback" 1 "(2) one switch read in TravelValues.cpp")
require_order("${travel_cpp}" "bool ProfessionTripGrindFallback(Player* bot)" "bool ProfessionTripTurnInWaiting(Player* bot, TravelTarget const* travelTarget)" "(2) defined before the gate")

# 3. The gate: the old rule counts first; only a finished quest it would hold meets the fallback; the proof line hourly; the result
#    rule unchanged otherwise (pinned also by turnin_work / noroute_gate). All turn_in users (gap gate, trip end, near gates) share it.
between("${travel_cpp}" "bool ProfessionTripTurnInWaiting(Player* bot, TravelTarget const* travelTarget)" "\n}\n" gate)
require_order("${gate}" "            ++finished;" "if (finished && ProfessionTripGrindFallback(bot))" "(3) the old count first")
require_order("${gate}" "if (finished && ProfessionTripGrindFallback(bot))" "return ai::quest_search::GatherYieldsToTurnIn(true, finished);" "(3) the skip before the result rule")
require_text("${gate}" "if (tracedAt == 0 || now >= tracedAt + ai::profession_travel::kGrindFallbackTraceSeconds)" "(3) the proof line at most hourly")
require_text("${gate}" "\"[ProfTravel] state=turnin_grind_fallback bot=%u level=%u quests=%u failures=%d\"" "(3) the proof line format")
count_text("${travel_cpp}" "ProfessionTripGrindFallback(bot)" 1 "(3) one gate call")
count_text("${travel_cpp}" "ProfessionTripTurnInWaiting(bot, " 4 "(3) the four turn_in users share the gate")
foreach(src request create)
  count_text("${${src}}" "professionTrainingTurnInGateSkipGrindFallback" 0 "(3) no switch read in ${src}")
endforeach()
