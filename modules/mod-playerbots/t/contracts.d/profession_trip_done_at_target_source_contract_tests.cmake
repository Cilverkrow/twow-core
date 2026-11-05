# twow-repo#333 Berufe P1-ab (OB-00 05.11.2026 after M1r run 9: 12 of 14 failed trainer trips learned a rank 1-14 s after the
# arrival and still held 198-634 s to reason=cap / reason=lost - the hold reason was "any phase-one trainer in the world", while a
# trip goes to one trainer). AiPlayerbot.ProfessionTraining.TripDoneAtTarget (code default 0; 1 all, 2 cohort B, 3 cohort A): on
# the trip's own travel target a trainer trip holds only while that trainer still teaches the bot a phase-one rank; the old rule
# stays as the else path (strangler: removed with the switch). Proof line "[ProfTravel] state=done_at_target" before the end line.
# This contract pins the switch (dist key, member, read with > 3 = off, sorted [ConfigEcho] entry), the three call sites behind the
# switch with the old path kept, the proof line and the pure rule's tests (t/profession_travel_policy_tests.cpp).

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
    message(FATAL_ERROR "#333 P1-ab TripDoneAtTarget: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-ab TripDoneAtTarget: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 P1-ab TripDoneAtTarget: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 P1-ab TripDoneAtTarget: region end not found after '${first}': '${second}'")
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
    message(FATAL_ERROR "#333 P1-ab TripDoneAtTarget: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/ProfessionTravelPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" request)
read_source("${PB_SOURCE_DIR}/TravelMgr.cpp" mgr)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_MODULE_DIR}/t/profession_travel_policy_tests.cpp" tests)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented with its proof line.
require_text("${dist}" "\nAiPlayerbot.ProfessionTraining.TripDoneAtTarget = 0\n" "dist key, default 0")
require_text("${dist}" "[ProfTravel] state=done_at_target kind=trainer bot= level= cohort= entry= left= trip_s= path=near|gap" "dist: the proof line")
require_text("${config_h}" "uint32 professionTrainingTripDoneAtTarget = 0;" "config member, default 0")
require_text("${config_cpp}" "professionTrainingTripDoneAtTarget = config.GetIntDefault(\"AiPlayerbot.ProfessionTraining.TripDoneAtTarget\", 0);\n    if (professionTrainingTripDoneAtTarget > 3)\n    {\n        sLog.outError(\"[ProfTravel] config key=TripDoneAtTarget value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", professionTrainingTripDoneAtTarget);\n        professionTrainingTripDoneAtTarget = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "            {\"ProfessionTraining.Travel\", Value(professionTrainingTravel)},\n            {\"ProfessionTraining.TripDoneAtTarget\", Value(professionTrainingTripDoneAtTarget)},\n            {\"ProfessionTraining.TripNoGrind\", Value(professionTrainingTripNoGrind)}," "parsed mode in [ConfigEcho], sorted")

# 1. The pure rule (no game types): none left = done; own target = only while it still teaches; no own target = the old rule.
between("${policy}" "inline bool TrainerTripReasonLasts(bool anyLeft, std::int32_t targetEntry, bool targetStillTeaches)\n{" "\n}\n" rule)
require_text("${rule}" "    if (!anyLeft)\n        return false;\n    return targetEntry == 0 || targetStillTeaches;" "(1) the rule")
require_text("${tests}" "TrainerTripReasonLasts(true, 5759, false)" "(1) the run-9 case in the policy tests")
require_text("${tests}" "TrainerTripReasonLasts(true, 0, false)" "(1) the no-target case in the policy tests")

# 2. The scoped reason: the trip's own trainer target, the switch read once, the proof line only on an open trip it ends.
between("${travel_cpp}" "bool TrainerTripReasonLastsAtTarget(PlayerbotAI* ai, bool onlyUnlearned)\n{" "\n}\n" scoped)
require_text("${scoped}" "status == TravelStatus::TRAVEL_STATUS_WORK || status == TravelStatus::TRAVEL_STATUS_COOLDOWN) &&\n        ai::ProfessionTripKindOf(travelTarget) == ai::profession_travel::kTripKindTrainer;" "(2) the own trainer target only")
require_text("${scoped}" "bool const lasts = ai::profession_travel::TrainerTripReasonLasts(!trainers.empty(), entry,\n        entry != 0 && std::find(trainers.begin(), trainers.end(), entry) != trainers.end());" "(2) the own entry in the same list")
require_text("${scoped}" "if (!lasts && !trainers.empty() && AI_VALUE2(int, \"manual int\", key + ai::profession_travel::kOpenSuffix))" "(2) the proof line only on an open trip the scope ends")
require_text("${scoped}" "\"[ProfTravel] state=done_at_target kind=trainer bot=%u level=%u cohort=%c entry=%d left=%u trip_s=%u path=%s\"" "(2) the proof line format")
count_text("${travel_cpp}" "professionTrainingTripDoneAtTarget" 1 "(2) one switch read in TravelValues.cpp")
count_text("${travel_cpp}" "TripDoneAtTargetApplies(ai)" 3 "(2) three call sites: tick reason, gap hold, near hold")
count_text("${travel_cpp}" "TrainerTripReasonLastsAtTarget(ai, " 3 "(2) three scoped calls")
require_order("${travel_cpp}" "bool TrainerTripReasonLastsAtTarget(PlayerbotAI* ai, bool onlyUnlearned)" "bool ProfessionTripReasonLasts(PlayerbotAI* ai, uint8 kind)" "(2) defined before its first use")

# 3. Each call site: the switch first, the old path kept as the else.
between("${travel_cpp}" "bool ProfessionTripReasonLasts(PlayerbotAI* ai, uint8 kind)\n{" "\n}\n" reason_lasts)
require_order("${reason_lasts}" "if (TripDoneAtTargetApplies(ai))\n        return TrainerTripReasonLastsAtTarget(ai, AI_VALUE2(int, \"manual int\", key + ai::profession_travel::kNearSuffix) != 0);" "return !ai::PhaseOneProfessionTrainers(ai, AI_VALUE2(int, \"manual int\", key + ai::profession_travel::kNearSuffix) != 0).empty();" "(3) tick reason: scoped first, old path after")
between("${travel_cpp}" "        if (name == \"trainer profession hold\")\n        {" "\n        }\n" gap_hold)
require_order("${gap_hold}" "if (!ai::cohort::Applies(mode, bot->GetGUIDLow()))\n                return false;" "if (TripDoneAtTargetApplies(ai))" "(3) gap hold: the travel switch first")
require_order("${gap_hold}" "if (TripDoneAtTargetApplies(ai))\n                return ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, \"trainer\", TrainerTripReasonLastsAtTarget(ai, false));" "return ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, \"trainer\", !PhaseOneProfessionTrainers(ai).empty());" "(3) gap hold: scoped, then the old path")
between("${travel_cpp}" "        if (name == \"trainer profession near hold\")\n        {" "\n        }\n" near_hold)
require_order("${near_hold}" "if (!ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTravel, bot->GetGUIDLow()))\n                return false;" "if (TripDoneAtTargetApplies(ai))" "(3) near hold: the travel switch first")
require_order("${near_hold}" "if (TripDoneAtTargetApplies(ai))\n                return ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, \"trainer\", TrainerTripReasonLastsAtTarget(ai, true));" "return ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, \"trainer\", !PhaseOneProfessionTrainers(ai, true).empty());" "(3) near hold: scoped (unlearned list), then the old path")

# 4. Nothing else reads the switch; the end lines stay as they are (their parsers: trips-check.awk, the P1-aa / P1-b3 contracts).
foreach(src request mgr ai_cpp)
  count_text("${${src}}" "professionTrainingTripDoneAtTarget" 0 "(4) no switch read in ${src}")
endforeach()
require_text("${travel_cpp}" "sLog.outBasic(\"[ProfTravel] state=end kind=%s bot=%u level=%u cohort=%c reason=%s trip_s=%u hold_over_s=%u path=%s\"," "(4) the hold end line unchanged")
