# twow-repo#333 Berufe P1-ad (OB-00 06.11.2026 after M1r run 10: a skinner's near tool trip to a vendor 89 yd away was expired
# 117 s after its start by the roster guild founding trip - RosterGuildFoundTripAction::Execute sets the travel target EXPIRED -
# and ended reason=lost, then an hour of cooldown: no knife). AiPlayerbot.ProfessionTraining.TripNoGuildTrip (code default 0;
# 1 all, 2 cohort B, 3 cohort A): while a profession trip is open (ai::ProfessionTripOpenKind, as P1-aa) the founding trip does not
# start; at a guild master the bot still buys. Proof line "[RosterGuild] event=buy_travel_deferred" (hourly per bot).
# Deletion target (ADR-0041 rule 4): goes with A3 step 3 (the supervisor owns the travel target).
# This contract pins the switch (dist key, member, read with > 3 = off, sorted [ConfigEcho] entry) and the guard's place.

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
    message(FATAL_ERROR "#333 P1-ad TripNoGuildTrip: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-ad TripNoGuildTrip: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 P1-ad TripNoGuildTrip: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 P1-ad TripNoGuildTrip: region end not found after '${first}': '${second}'")
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
    message(FATAL_ERROR "#333 P1-ad TripNoGuildTrip: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create)
read_source("${PB_SOURCE_DIR}/strategy/actions/ChooseTargetActions.cpp" choose_target)
read_source("${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel_cpp)
read_source("${PB_SOURCE_DIR}/TravelMgr.cpp" mgr)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented with its deletion target.
require_text("${dist}" "\nAiPlayerbot.ProfessionTraining.TripNoGuildTrip = 0\n" "dist key, default 0")
require_text("${dist}" "goes with A3 step 3, when the supervisor owns the travel target" "dist: deletion target (ADR-0041 rule 4)")
require_text("${config_h}" "uint32 professionTrainingTripNoGuildTrip = 0;" "config member, default 0")
require_text("${config_cpp}" "professionTrainingTripNoGuildTrip = config.GetIntDefault(\"AiPlayerbot.ProfessionTraining.TripNoGuildTrip\", 0);\n    if (professionTrainingTripNoGuildTrip > 3)\n    {\n        sLog.outError(\"[ProfTravel] config key=TripNoGuildTrip value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", professionTrainingTripNoGuildTrip);\n        professionTrainingTripNoGuildTrip = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "            {\"ProfessionTraining.TripNoGrind\", Value(professionTrainingTripNoGrind)},\n            {\"ProfessionTraining.TripNoGuildTrip\", Value(professionTrainingTripNoGuildTrip)},\n            {\"ProfessionTraining.TurnInGateSkipGrindFallback\"" "parsed mode in [ConfigEcho], sorted")

# 1. The guard in the founding trip's isUseful: after the guild-master buy (buying stays) and the PREPARE check, before the
#    guild-master target check and the retry throttle; the open trip from the same helper as P1-aa; Execute unchanged.
between("${create}" "bool RosterGuildFoundTripAction::isUseful()" "bool RosterGuildFoundTripAction::Execute(" useful)
set(guard "    if (ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTripNoGuildTrip, bot->GetGUIDLow()))\n    {\n        if (uint8 const tripKind = ai::ProfessionTripOpenKind(ai))\n        {")
require_text("${useful}" "${guard}" "(1) switch and cohort, then the open trip")
require_text("${useful}" "                sLog.outBasic(\"[RosterGuild] event=buy_travel_deferred reason=profession_trip bot=%u level=%u kind=%u\", bot->GetGUIDLow(),\n                    bot->GetLevel(), uint32(tripKind));\n            return false;" "(1) the proof line, then no trip")
require_text("${useful}" "if (RosterGuildPlan::IsDue(ai, \"roster guild trip deferred trace\", HOUR))" "(1) the proof line at most hourly")
require_order("${useful}" "if (PetitionerNearby())" "${guard}" "(1) buying at a guild master comes first")
require_order("${useful}" "if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)" "${guard}" "(1) after the PREPARE check")
require_order("${useful}" "${guard}" "destination->HasNpcFlag(UNIT_NPC_FLAG_PETITIONER)" "(1) before the guild-master target check")
require_order("${useful}" "${guard}" "RosterGuildRetrySeconds" "(1) before the retry throttle")
count_text("${create}" "professionTrainingTripNoGuildTrip" 1 "(1) one switch read in GuildCreateActions.cpp")
require_text("${create}" "#include \"playerbot/strategy/values/TravelValues.h\"" "(1) ProfessionTripOpenKind declared")
between("${create}" "bool RosterGuildFoundTripAction::Execute(" "bool RosterGuildOfferRemoteAction::isUseful()" exec)
count_text("${exec}" "professionTrainingTripNoGuildTrip" 0 "(1) Execute unchanged")
require_text("${exec}" "AI_VALUE(TravelTarget*, \"travel target\")->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "(1) Execute still expires the target (why the guard sits in isUseful)")

# 2. The same open-trip helper as P1-aa (no second definition of an open trip); nothing else reads the switch.
require_text("${choose_target}" "ai::ProfessionTripOpenKind(ai))" "(2) P1-aa uses the same helper")
foreach(src choose_target travel_cpp mgr)
  count_text("${${src}}" "professionTrainingTripNoGuildTrip" 0 "(2) no switch read in ${src}")
endforeach()
