cmake_policy(VERSION 3.16)
# twow-repo#601 no-route gate (CLI-Deep 29.10.2026, OB-00 go; CLI-Berufe M1r6b: the profession trips' turn_in gate was held by finished
# quests WITHOUT a route - 55002 54 gates, bots 86 / 4517 with 2 of the 3 park counts and no further request). AiPlayerbot.
# ProfessionTraining.TurnInGateSkipNoRoute (default 0): a finished quest whose last turn-in route request judged every taker and found
# no route no longer holds the gate until a request finds a taker. Pins the key, the mark / unmark rule and its effect line (switch on
# only), the feed before #485 clears the offered list, the fail-closed gate condition, and the gate line fields quest= nearest=
# nearest_entry= skip_no_route=.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "no-route gate: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "no-route gate: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "no-route gate: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "no-route gate: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "no-route gate: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "no-route gate: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/values/TravelValues.cpp" values)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 professionTrainingTurnInGateSkipNoRoute = 0;" "member default off")
require_text("${config_cpp}" "professionTrainingTurnInGateSkipNoRoute = config.GetIntDefault(\"AiPlayerbot.ProfessionTraining.TurnInGateSkipNoRoute\", 0);" "default 0")
require_text("${config_cpp}" "if (professionTrainingTurnInGateSkipNoRoute > 2)" "unknown mode checked")
require_text("${config_cpp}" "{\"ProfessionTraining.TurnInGateSkipNoRoute\", Value(professionTrainingTurnInGateSkipNoRoute)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.ProfessionTraining.TurnInGateSkipNoRoute = 0\n" "documented off")

# 2. Mark / unmark: switch + cohort first; a taker unmarks; only a no-route judgement marks (bounded); the effect line per request
#    without a taker, switch on only.
between("${travel}" "void TravelTarget::NoteTurnInRouteResult(" "\n}\n" note_fn)
require_text("${note_fn}" "if (!bot || questIds.empty() || !ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTurnInGateSkipNoRoute, bot->GetGUIDLow()))\n        return;" "switch and cohort first")
require_text("${note_fn}" "if (takerFound)\n    {\n        for (uint32 questId : questIds)\n            noRouteTurnIns.erase(questId);\n        return;\n    }" "a found taker unmarks")
require_text("${note_fn}" "if (noRoute)\n    {" "only a no-route judgement marks")
require_text("${note_fn}" "if (noRouteTurnIns.size() > 32)\n            noRouteTurnIns.clear();" "bounded")
require_text("${note_fn}" "[ProfTravel] state=turnin_no_route bot=%u level=%u quests=%u mark=%u" "the effect line")
require_text("${travel_h}" "std::set<uint32> noRouteTurnIns;" "per-bot set")

# 3. The feed: the same CountsAsNoRoute judgement right before each #485 NoteNoRouteTurnIns (which clears the list); reads the list
#    only with the switch on and never clears it.
between("${choose}" "void NoteTurnInRouteResults(AiObjectContext* context, TravelTarget* travelTarget, bool noRoute, bool takerFound)" "\n}\n" feed_fn)
require_text("${feed_fn}" "if (!sPlayerbotAIConfig.professionTrainingTurnInGateSkipNoRoute)\n        return;" "off: nothing read")
forbid_text("${feed_fn}" "SET_AI_VALUE2" "the feed never clears the list")
require_order("${choose}" "NoteTurnInRouteResults(context, travelTarget, ai::turnin_park::CountsAsNoRoute(ai::turnin_park::RouteOutcome::NoTarget,"
  "if (NoteNoRouteTurnIns(context, travelTarget, ai::turnin_park::CountsAsNoRoute(ai::turnin_park::RouteOutcome::NoTarget," "no target: fed before #485")
require_text("${choose}" "sPlayerbotAIConfig.questFirstProgressionTurnInParkCountsRouteDanger), false);" "no target: no taker found")
require_text("${choose}" "bool const takerFound = newTarget.GetDestination()->GetPurpose() == TravelDestinationPurpose::QuestTaker;" "taker or fallback")
require_order("${choose}" "sPlayerbotAIConfig.questFirstProgressionTurnInParkCountsRouteDanger), takerFound);"
  "        NoteNoRouteTurnIns(context, travelTarget, ai::turnin_park::CountsAsNoRoute(" "found target: fed before #485")

# 4. The gate: a marked quest no longer counts, fail closed; the suppressed rule stays as it was.
between("${values}" "bool ProfessionTripTurnInWaiting(Player* bot, TravelTarget const* travelTarget)" "\n}\n" gate)
require_text("${gate}" "ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTurnInGateSkipNoRoute, bot->GetGUIDLow()) ? travelTarget : nullptr;" "switch and cohort")
require_text("${gate}" "(!noRoute || !noRoute->IsTurnInNoRoute(questId)) &&\n            (!suppressed || !suppressed->IsTurnInRouteSuppressedForQuest(questId)))\n            ++finished;" "a marked quest does not wait; fail closed otherwise")

# 5. The gate lines: fields at the end of both; the nearest walk only for a written not_near line.
require_text("${values}" "dropped_home_zone=%u zone=%u quest=%u nearest=%u nearest_entry=%d skip_no_route=%u\",\n            bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), refused ? refused : \"start\", bot->GetMoney()," "trainer line fields")
require_text("${values}" "dropped_home_zone=%u zone=%u quest=%u nearest=%u nearest_entry=%d skip_no_route=%u\",\n            bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), refused ? refused : \"start\", tool," "tool line fields")
require_text("${values}" "if (refused && std::string(refused) == \"not_near\")\n            NearestTripEntry(ai, ai::PhaseOneProfessionTrainers(ai, true), TravelDestinationPurpose::Trainer, nearest, nearestEntry);" "trainer nearest at not_near only")
require_text("${values}" "if (refused && std::string(refused) == \"not_near\")\n            NearestTripEntry(ai, ai::GatherToolVendors(ai, tool), TravelDestinationPurpose::Vendor, nearest, nearestEntry);" "vendor nearest at not_near only")
