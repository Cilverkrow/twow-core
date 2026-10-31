cmake_policy(VERSION 3.16)
# twow-repo#601 turn_in hold (CLI-Deep 28.10.2026, OB-00 go, cli-quest-organic/turnin-hold/CAUSE.md): bots reached the taker of a
# finished quest (41747 / 62634) and lost the hand-in to "attack anything" once the arrival set WORK; low-level bots died, the route
# was death-suppressed for an hour and the profession gate held on turn_in the whole time. Four switches, all default 0:
#   QuestLoop.RelationWorkNoGrind      - in WORK at an open giver / taker, grind candidates without quest need are skipped;
#   QuestLoop.RelationArriveRpgTarget  - on arrival the NPC becomes the rpg target with the hand-in / accept as next rpg action;
#   QuestLoop.TurnInWorkDeathNotRoute  - a death in WORK at the taker is a turn-in failure (#485 park), no death_suppressed;
#   ProfessionTraining.TurnInGateSkipSuppressed - the "open turn-in waits" rule skips a quest whose route is suppressed for it.
# Pins the keys, the rules and their places.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "turnin work: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "turnin work: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "turnin work: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "turnin work: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "turnin work: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel)
read_source("QuestLoopPolicy.h" policy)
read_source("ProgressAwareTurnInRecoveryPolicy.h" recovery)
read_source("strategy/values/GrindTargetValue.cpp" grind)
read_source("strategy/values/TravelValues.cpp" values)

# 1. Keys: neutral default 0, an unknown mode is off, documented, echoed.
# twow-repo#601 crossover (OB-00 31.10.2026): RelationWorkNoGrind also takes 3 = cohort A (the second arm of a crossover unit).
foreach(pair "QuestLoop.RelationWorkNoGrind;questLoopRelationWorkNoGrind;RelationWorkNoGrind;QuestLoop;3;0 off, 1 all, 2 cohort B, 3 cohort A"
             "QuestLoop.RelationArriveRpgTarget;questLoopRelationArriveRpgTarget;RelationArriveRpgTarget;QuestLoop;2;0 off, 1 all, 2 cohort B"
             "QuestLoop.TurnInWorkDeathNotRoute;questLoopTurnInWorkDeathNotRoute;TurnInWorkDeathNotRoute;QuestLoop;2;0 off, 1 all, 2 cohort B"
             "ProfessionTraining.TurnInGateSkipSuppressed;professionTrainingTurnInGateSkipSuppressed;TurnInGateSkipSuppressed;ProfTravel;2;0 off, 1 all, 2 cohort B")
  list(GET pair 0 key)
  list(GET pair 1 member)
  list(GET pair 2 short)
  list(GET pair 3 prefix)
  list(GET pair 4 maxmode)
  list(GET pair 5 modes)
  require_text("${config_h}" "uint32 ${member} = 0;" "${key}: member default off")
  require_text("${config_cpp}" "${member} = config.GetIntDefault(\"AiPlayerbot.${key}\", 0);" "${key}: default 0")
  require_text("${config_cpp}" "if (${member} > ${maxmode})\n    {\n        sLog.outError(\"[${prefix}] config key=${short} value=%u result=off (${modes})\", ${member});\n        ${member} = 0;\n    }" "${key}: unknown mode is off")
  require_text("${config_cpp}" "{\"${key}\", Value(${member})}," "${key}: ConfigEcho entry")
  require_text("${conf_dist}" "\nAiPlayerbot.${key} = 0\n" "${key}: documented")
endforeach()

# 2. Open relation: the destination's own quest - taker finished and not handed in; giver not in the log, not rewarded, takeable.
between("${travel}" "bool ai::QuestRelationOpen(Player* bot, TravelDestination const* destination)" "\n}\n" open)
require_text("${open}" "if (!bot || !quest || bot->GetQuestRewardStatus(quest->GetQuestId()))\n        return false;" "rewarded: closed")
require_text("${open}" "if (relation->GetRelation())\n        return status == QUEST_STATUS_COMPLETE;" "taker: finished")
require_text("${open}" "return status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE && bot->CanTakeQuest(quest, false);" "giver: takeable, no message")

# 3. RelationWorkNoGrind: computed once per pass, the switch first; the skip only for candidates without quest need, before the
#    gap window and every urand; attackers are taken before the candidate loop.
require_text("${policy}" "return relationWorkOpen && !needForQuest;" "grind skip rule")
require_text("${grind}" "bool const relationWorkOpen = ai::cohort::Applies(sPlayerbotAIConfig.questLoopRelationWorkNoGrind, bot->GetGUIDLow()) &&\n        travelTargetWorking && travelTarget && !group && rosterOnItsOwn && QuestRelationOpen(bot, travelTarget->GetDestination());" "the gate: switch first, WORK, roster bot on its own, no group, open relation")
require_order("${grind}" "logGrind(unit, \"(hostile) selected.\");" "bool const relationWorkOpen =" "attackers before the rule")
between("${grind}" "        if (entry && !needForQuest)\n        {" "else if (urand(0, 100) < 75)" plain)
require_text("${plain}" "if (ai::quest_loop::RelationWorkGrindSkipped(relationWorkOpen, needForQuest))\n            {\n                logGrind(unit, \"ignored (QuestLoop.RelationWorkNoGrind: the hand-in / accept at the NPC first).\");\n                continue;\n            }" "the skip")
require_order("${plain}" "RelationWorkGrindSkipped(relationWorkOpen, needForQuest)" "ai::quest_grind_mode::GapTargetSkipped" "before the gap window")
require_order("${plain}" "RelationWorkGrindSkipped(relationWorkOpen, needForQuest)" "urand(0, 100) < 20" "before the 20 % skip")

# 4. RelationArriveRpgTarget: on arrival before WORK, roster bot on its own without a group; one grid search; rpg target + next action.
require_text("${policy}" "return relation ? \"rpg end quest\" : \"rpg start quest\";" "the rpg action")
between("${travel}" "TraceQuestCommit(tDestination, \"progress\", \"arrived\");" "SetStatus(TravelStatus::TRAVEL_STATUS_WORK);\n            return;" arrive)
require_text("${arrive}" "if (ai::cohort::Applies(sPlayerbotAIConfig.questLoopRelationArriveRpgTarget, bot->GetGUIDLow()) &&\n                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() && !bot->GetGroup())\n                SetRelationRpgTarget();" "arrival gate")
between("${travel}" "void TravelTarget::SetRelationRpgTarget()" "\n}\n" rpg)
require_text("${rpg}" "if (!relation || relation->GetEntry() <= 0 || !QuestRelationOpen(bot, tDestination))\n        return;" "a creature giver / taker, still open")
require_text("${rpg}" "Creature* npc = bot->FindNearestCreature(uint32(relation->GetEntry()), INTERACTION_DISTANCE * 4.0f);" "one bounded grid search")
require_text("${rpg}" "context->GetValue<GuidPosition>(\"rpg target\")->Set(GuidPosition(npc));" "rpg target")
require_text("${rpg}" "context->GetValue<std::string>(\"next rpg action\")->Set(quest_loop::RelationArriveRpgAction(relation->GetRelation()));" "next rpg action")
require_text("${rpg}" "TraceQuestCommit(tDestination, \"progress\", \"rpg_target\");" "trace")
require_text("${travel_h}" "void SetRelationRpgTarget();" "declared")

# 5. TurnInWorkDeathNotRoute: before RecordDeathOnRoute; WORK, switch, parking on; a turn-in failure, EXPIRED, one line per death.
require_text("${policy}" "return applies && working && parkingOn;" "work death rule")
between("${travel}" "void TravelTarget::OnDeathOnTurnInRoute()" "\n}\n" death)
require_text("${death}" "if (quest_loop::WorkDeathIsTurnInFailure(\n            ai::cohort::Applies(sPlayerbotAIConfig.questLoopTurnInWorkDeathNotRoute, bot->GetGUIDLow()),\n            m_status == TravelStatus::TRAVEL_STATUS_WORK, sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures != 0))" "gate")
require_text("${death}" "sLog.outBasic(\"[QuestFirstRoute] state=work_death bot=%u level=%u quest=%u target_entry=%u map=%u action=turnin_failure\"," "the line")
require_text("${death}" "NoteTurnInFailure(questId, \"work_death\");\n        TraceQuestCommit(tDestination, \"abandon\", \"work_death\");\n        SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);\n        return;" "a turn-in failure, EXPIRED")
require_order("${death}" "WorkDeathIsTurnInFailure" "turnin_recovery::RecordDeathOnRoute(" "before the route rule")

# 6. TurnInGateSkipSuppressed: quest ids in both suppression slots (every setter), the per-quest check, the gate predicate.
require_text("${recovery}" "std::uint32_t suppressedQuestId = 0;" "stall slot quest")
require_text("${recovery}" "std::uint32_t deathSuppressedQuestId = 0;" "death slot quest")
require_text("${recovery}" "state.deathSuppressedUntil = now + cooldownMs;\n    state.deathSuppressedQuestId = questId;" "death setter")
require_text("${recovery}" "state.suppressUntil = current.now + cooldownSeconds;\n    state.suppressedQuestId = current.questId;" "stall setter")
require_text("${recovery}" "return questId != 0 && (stalled || died);" "quest 0 never matches")
require_text("${travel}" "turnInRecovery.suppressUntil = observation.now + transport_stall::CooldownMs;\n            turnInRecovery.suppressedQuestId = observation.questId;" "transport abandon setter")
require_text("${travel}" "turnInRecovery.suppressedQuestId = stalledQuest ? stalledQuest->GetQuestId() : 0;" "near stall setter")
string(REGEX MATCHALL "turnInRecovery\\.suppressUntil = " stall_setters "${travel}")
list(LENGTH stall_setters stall_setter_count)
string(REGEX MATCHALL "turnInRecovery\\.suppressedQuestId = " quest_setters "${travel}")
list(LENGTH quest_setters quest_setter_count)
if(NOT stall_setter_count EQUAL quest_setter_count)
  message(FATAL_ERROR "turnin work: every stall suppression in TravelMgr.cpp sets its quest (${stall_setter_count} vs ${quest_setter_count})")
endif()
require_text("${travel}" "return turnin_recovery::IsSuppressedForQuest(turnInRecovery, WorldTimer::getMSTime(), questId);" "per-quest check")
between("${values}" "bool ProfessionTripTurnInWaiting(Player* bot, TravelTarget const* travelTarget)" "\n}\n" gate)
require_text("${gate}" "TravelTarget const* suppressed = travelTarget &&\n        ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTurnInGateSkipSuppressed, bot->GetGUIDLow()) ? travelTarget : nullptr;" "gate switch")
require_text("${gate}" "(!suppressed || !suppressed->IsTurnInRouteSuppressedForQuest(questId)))\n            ++finished;" "a suppressed quest does not wait; fail closed otherwise")
require_text("${gate}" "return ai::quest_search::GatherYieldsToTurnIn(true, finished);" "the result rule unchanged")
