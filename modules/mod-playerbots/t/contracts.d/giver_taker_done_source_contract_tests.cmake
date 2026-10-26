cmake_policy(VERSION 3.16)
# twow-repo#601 cr2-C gap analysis (CLI-Deep 26.10.2026, OB-00 go): AiPlayerbot.QuestLoop.GiverTakerDone (default 0) - a quest giver
# trip whose quest is now taken, or a taker trip whose quest is turned in, went inactive because it succeeded and ends at once
# (EXPIRED, [QuestCommit] drop reason=relation_done) instead of the destination's 60 s COOLDOWN at the NPC. Pins the key, the
# success rule, the place in the inactive drop (after the S5a band giver branch, before the COOLDOWN path) and its gate.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "giver taker done: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "giver taker done: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "giver taker done: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "giver taker done: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "giver taker done: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)
read_source("QuestLoopPolicy.h" policy)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 questLoopGiverTakerDone = 0;" "member default off")
require_text("${config_cpp}" "questLoopGiverTakerDone = config.GetIntDefault(\"AiPlayerbot.QuestLoop.GiverTakerDone\", 0);" "default 0")
require_text("${config_cpp}" "if (questLoopGiverTakerDone > 2)" "unknown mode checked")
require_text("${config_cpp}" "questLoopGiverTakerDone = 0;\n    }" "unknown mode is off")
require_text("${config_cpp}" "{\"QuestLoop.GiverTakerDone\", Value(questLoopGiverTakerDone)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.QuestLoop.GiverTakerDone = 0\n" "documented")

# 2. Success only: giver - in the log or rewarded; taker - no longer in the log; in the log = incomplete / complete, not rewarded.
require_text("${policy}" "return relation == 0 ? (questInLog || questRewarded) : !questInLog;" "success rule")
between("${travel}" "bool GiverTakerTripDone(Player* bot, TravelDestination const* destination)" "\n}\n" helper)
require_text("${helper}" "QuestRelationTravelDestination const* relation = dynamic_cast<QuestRelationTravelDestination const*>(destination);\n    if (!bot || !relation)\n        return false;" "giver / taker destinations only")
require_text("${helper}" "bool const inLog = !rewarded && (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE);" "in the log")
require_text("${helper}" "return quest_loop::GiverTakerTripDone(relation->GetRelation(), inLog, rewarded);" "the policy decides")

# 3. The inactive drop: after the gap grind and S5a band giver branches, before the COOLDOWN path; roster bot on its own, cohort;
#    EXPIRED with one drop line, the request value cleared, no failure and no COOLDOWN.
between("${travel}" "if (destinationInactive && ai::quest_band::GiverTripDoneFor(ai, bot, tDestination))" "SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" done)
require_text("${done}" "if (destinationInactive && ai::cohort::Applies(sPlayerbotAIConfig.questLoopGiverTakerDone, bot->GetGUIDLow()) &&\n                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() && !groupMember &&\n                GiverTakerTripDone(bot, tDestination))" "gate: switch, cohort, roster bot on its own, no group trip, success")
require_order("${done}" "GiverTakerTripDone(bot, tDestination))" "TraceQuestCommit(tDestination, \"drop\", destinationInactive ? \"destination_inactive\" : \"conditions_inactive\");" "before the COOLDOWN drop")
require_text("${done}" "TraceQuestCommit(tDestination, \"drop\", \"relation_done\");\n                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);\n                ai->GetAiObjectContext()->ClearValues(\"no active travel destinations\");\n                return;" "EXPIRED, request free, return")
string(FIND "${done}" "relation_done" relation_at)
string(SUBSTRING "${done}" ${relation_at} 200 after_relation)
string(FIND "${after_relation}" "NoteQuestObjectiveFailure" failure_after)
if(NOT failure_after EQUAL -1)
  message(FATAL_ERROR "giver taker done: a success counts no failure")
endif()
