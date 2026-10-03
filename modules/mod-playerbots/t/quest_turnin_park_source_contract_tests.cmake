# twow-repo#485: turn-ins that keep failing are parked. Pins the hooks that count a failed
# turn-in, the consumers that skip a parked one, the refusal of a parked taker in CopyTarget
# (BotBrain intents included), the neutral config keys and the pure policy header.
# Paths only through PB_SOURCE_DIR (never CMAKE_SOURCE_DIR: twow-repo builds the core under /src/core).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

# Fails unless `first` occurs in `text` before `second`.
function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "Wrong order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `first` up to `second` (both required, in this order), e.g. one body.
function(extract_between text first second out description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "Wrong order (${description}): '${first}' must come before '${second}'")
  endif()
  math(EXPR part_length "${second_at} - ${first_at}")
  string(SUBSTRING "${text}" ${first_at} ${part_length} part)
  set(${out} "${part}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/QuestTurnInParkPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/TravelMgr.h" travel_h)
file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/strategy/actions/MoveToTravelTargetAction.cpp" move_to)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel_values)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.h" ai_header)
file(READ "${PB_SOURCE_DIR}/QuestSearchPolicy.h" search_policy)
# LF line ends (a Windows checkout has CRLF), so a needle below may span lines.
string(REPLACE "\r\n" "\n" travel_mgr "${travel_mgr}")
string(REPLACE "\r\n" "\n" choose "${choose}")
string(REPLACE "\r\n" "\n" config_template "${config_template}")

# The policy is pure: std headers only, not in the botpch.h chain.
require_text("${policy}" "namespace ai::turnin_park" "policy namespace")
require_text("${policy}" "#include <array>" "std-only policy")
require_text("${policy}" "#include <cstdint>" "std-only policy")
reject_text("${policy}" "#include \"" "project include in the pure policy")
reject_text("${ai_header}" "QuestTurnInParkPolicy.h" "policy in the botpch.h chain (PlayerbotAI.h)")
reject_text("${config_header}" "QuestTurnInParkPolicy.h" "policy in the botpch.h chain (PlayerbotAIConfig.h)")

# The book lives in the bot's travel target (a manual value, never evicted).
require_text("${travel_h}" "#include \"QuestTurnInParkPolicy.h\"" "policy for the travel target member")
require_text("${travel_h}" "turnin_park::Book turnInParks;" "per-bot park book")
require_text("${travel_h}" "bool NoteTurnInFailure(uint32 questId, char const* reason);" "failure entry point that reports a park")
require_text("${travel_h}" "bool IsTurnInParked(uint32 questId) const;" "park query")
require_text("${travel_h}" "void OnMoveRetryCooldown();" "move retry cooldown hook")
require_text("${travel_h}" "bool CopyTarget(TravelTarget* const target);" "CopyTarget reports a refused choice")

# Counting: only with the key on, only roster bots on their own (critic B1.7: by GUID), only a
# finished quest not handed in yet (Player::RewardQuest keeps a handed-in quest COMPLETE).
extract_between("${travel_mgr}" "bool TravelTarget::NoteTurnInFailure(uint32 questId, char const* reason)"
  "bool TravelTarget::IsTurnInParked(uint32 questId) const" note_failure "failure counting before the park query")
require_text("${note_failure}" "if (!maxFailures || !questId || !bot)" "key 0 counts nothing")
require_text("${note_failure}" "if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || ai->HasRealPlayerMaster())" "roster bots on their own")
require_text("${note_failure}" "if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId))" "only a finished quest not handed in")
require_text("${note_failure}" "turnin_park::RecordFailure(turnInParks, questId, WorldTimer::getMSTime(), maxFailures," "policy decides the park")
require_text("${note_failure}" "sPlayerbotAIConfig.questFirstProgressionTurnInParkWindowSeconds * IN_MILLISECONDS" "window key read")
require_text("${note_failure}" "sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds * IN_MILLISECONDS" "park key read")
require_text("${note_failure}" "[QuestFirstRoute] state=parked bot=%u level=%u quest=%u reason=%s park_seconds=%u parked=%u" "visible park line")
require_order("${note_failure}" "[QuestFirstRoute] state=parked" "return true;" "true only for a park")

extract_between("${travel_mgr}" "bool TravelTarget::IsTurnInParked(uint32 questId) const"
  "void TravelTarget::OnMoveRetryCooldown()" park_queries "park queries before the move retry hook")
require_text("${park_queries}" "if (!sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures || ai->HasRealPlayerMaster())" "no park applies when off or led by a real master")
require_text("${park_queries}" "return turnin_park::IsParked(turnInParks, questId, WorldTimer::getMSTime()) &&\n        bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(questId);"
  "a park applies to a quest still finished and not handed in")
require_text("${park_queries}" "if (entry.questId && IsTurnInParked(entry.questId))" "parked= counts the parks that apply")
require_order("${park_queries}" "if (money < 0 && bot->GetMoney() < uint32(-money))"
  "return bot->CanRewardQuest(quest, false) && !IsTurnInParked(questId);" "money first: CanRewardQuest sends a packet when it is short")
require_text("${park_queries}" "return bot->CanRewardQuest(quest, false) && !IsTurnInParked(questId);" "open turn-in = taker fetch test (critic B1.2)")

# The failure sources (3a)-(3d).
require_order("${travel_mgr}" "TraceQuestCommit(tDestination, \"abandon\", \"stall_suppressed\");"
  "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"stall\");" "stall counted at recovery stage 2")
require_text("${travel_mgr}" "NoteTurnInFailure(questId, \"death\");" "death suppression on the route counted")
require_order("${travel_mgr}" "void TravelTarget::OnWorkTimeout()"
  "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"work_timeout\");" "work timeout of a turn-in counted")
require_order("${travel_mgr}" "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"work_timeout\");"
  "if (!sPlayerbotAIConfig.questWorkTimeoutsMax || !dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" "turn-in block first, #405 check unchanged")
extract_between("${travel_mgr}" "void TravelTarget::OnMoveRetryCooldown()" "void TravelTarget::DecRetry(bool isMove)" move_cooldown "move retry hook before DecRetry")
require_order("${move_cooldown}" "TraceQuestCommit(tDestination, \"blocked\", \"move_retry_cooldown\");" "if (!IsProgressAwareTurnIn())" "visible move retry cooldown for every quest target")
require_order("${move_cooldown}" "if (bot->IsInCombat() || !bot->IsAlive() || bot->IsTaxiFlying() || bot->GetTransport() || ai->HasRealPlayerMaster())"
  "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"move_failed\");" "move failures counted unless the bot is held up")
require_order("${move_to}" "if (target->IsMaxRetry(true))" "target->OnMoveRetryCooldown();" "hook inside the max-retry branch")
require_order("${move_to}" "target->OnMoveRetryCooldown();" "target->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN)" "hook before the cooldown")

# Critic B1.6: CopyTarget refuses the taker of a parked turn-in before it copies anything,
# so a BotBrain turn_in_quest intent (setNewTarget -> CopyTarget) cannot bypass the park. A
# group copy follows a member's target and is taken up.
require_text("${travel_mgr}" "bool TravelTarget::CopyTarget(TravelTarget* const target) {" "CopyTarget returns the verdict")
require_text("${travel_mgr}" "if (taker && taker->GetRelation() && !target->IsGroupCopy() && IsTurnInParked(taker->GetQuestId()))" "a group copy is not refused")
require_order("${travel_mgr}" "TraceQuestCommit(taker, \"drop\", \"parked_intent\");" "SetTarget(target->tDestination, target->wPosition);" "refusal before the copy")
require_text("${choose}" "if (!oldTarget->CopyTarget(newTarget))" "setNewTarget stops on a refused choice")

# Critic B1.1: move retries are reset when the destination changes, and DecRetry takes the right
# counter - both only with the key on; with 0 the old one-liner stays for every bot.
require_order("${travel_mgr}" "if (sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures && target->tDestination != tDestination)"
  "SetTarget(target->tDestination, target->wPosition);" "move retries reset before the copy")
reject_text("${travel_h}" "void DecRetry(bool isMove) { if (isMove && moveRetryCount > 0)" "old inline DecRetry")
extract_between("${travel_mgr}" "void TravelTarget::DecRetry(bool isMove)" "bool TravelTarget::IsDestinationActive()" dec_retry "DecRetry out of line")
require_order("${dec_retry}"
  "    if (!sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures)\n    {\n        if (isMove && moveRetryCount > 0) moveRetryCount--; else if (extendRetryCount > 0) extendRetryCount--;\n        return;\n    }"
  "    if (isMove)\n    {\n        if (moveRetryCount)\n            --moveRetryCount;\n    }\n    else if (extendRetryCount)\n        --extendRetryCount;"
  "key 0: the old DecRetry; key on: a move takes only a move retry, a refresh retry only without a move")

# Consumers (4a)-(4e).
require_order("${choose}" "if (player->CanRewardQuest(questTemplate, false))" "if (parkTarget->IsTurnInParked(questId))" "no taker fetch for a parked turn-in")
require_order("${choose}" "if (parkTarget->IsTurnInParked(questId))" "flag = (uint32)TravelDestinationPurpose::QuestTaker;" "park check inside the taker branch")
require_text("${choose}" "bool const turnInParking = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures > 0;" "key 0: hand-in-only gate unchanged")
require_text("${choose}" "questStatus.m_status == QUEST_STATUS_COMPLETE && (!turnInParking || parkTarget->IsTurnInOpen(questId))" "hand-in-only gate counts open turn-ins only (critic B1.2)")
require_text("${choose}" "finished > 0" "turn-in-first gate unchanged")
require_text("${choose}" "\"future travel handin quests\"" "turn-in-only quests handed to the choice")
require_text("${choose}" "if (parkTarget->IsTurnInOpen(uint32(std::get<1>(fetch))))" "own open turn-ins only (a handed-in quest stays COMPLETE)")
require_text("${choose}" "\"no_route\"" "no route counted")
require_text("${choose}" "target->IsTurnInParked(oldTaker->GetQuestId())" "refresh does not bring a parked taker back")
require_text("${choose}" "givers=%u strategy=%s parked=%u" "parked count in the request trace")

# (3e) no_route in the no-target branch: only when every taker was judged and no filter that lifts
# by itself took one (critic B1.3; route danger only with TurnInParkCountsRouteDanger). A park
# there ends the backoff - only then. After a choice, the quest giver fallback counts.
# Route danger = all three deferrals: cross map, zone level and the danger map (logged as
# reason=route_danger detail=death_cluster). Not judged = a stale list, a random range skip and the
# candidates a resumed choice (#416) skipped - not the local quest hub filter.
require_text("${choose}" "if (NoteNoRouteTurnIns(context, travelTarget, ai::turnin_park::CountsAsNoRoute(ai::turnin_park::RouteOutcome::NoTarget,"
  "no_route counted when the choice took nothing")
extract_between("${choose}" "ai::turnin_park::RouteOutcome::NoTarget," "// Hotfix 8.1: a route was found - no more backoff." no_target "no_route in the no-target branch")
require_text("${no_target}" "chooseBudgetExceeded, lastRejects.crossMap + lastRejects.zoneLevel + lastRejects.dangerMap, lastRejects.turnInSuppressed,"
  "all route danger deferrals and suppressed routes (critic B1.3)")
require_text("${no_target}"
  "lastRejects.movedAway + lastRejects.rangeSkip + lastRejects.resumeSkipped,\n                sPlayerbotAIConfig.questFirstProgressionTurnInParkCountsRouteDanger)))\n            {\n                SET_AI_VALUE2(bool, \"no active travel destinations\", futureTravelPurpose, false);\n                SET_AI_VALUE2(int, \"manual int\", \"quest route failures\", 0);\n                SET_AI_VALUE2(int, \"manual int\", \"quest route backoff until\", 0);\n            }"
  "every way a taker goes unjudged and the route danger switch; only a park ends the backoff")
extract_between("${choose}" "// Hotfix 8.1: a route was found - no more backoff." "setNewTarget(requester, &newTarget, travelTarget);" chosen "no_route after a choice")
require_text("${chosen}" "ai::turnin_park::RouteOutcome::Taker : ai::turnin_park::RouteOutcome::Fallback," "the quest giver fallback counts, a taker does not")
require_text("${chosen}" "chooseBudgetExceeded, lastRejects.crossMap + lastRejects.zoneLevel + lastRejects.dangerMap, lastRejects.turnInSuppressed,"
  "same route danger inputs after a choice")
require_text("${chosen}"
  "lastRejects.movedAway + lastRejects.rangeSkip + lastRejects.resumeSkipped,\n            sPlayerbotAIConfig.questFirstProgressionTurnInParkCountsRouteDanger));"
  "same unjudged inputs after a choice")
# Exactly these two calls, each with all groups - no call site with fewer reasons.
string(REGEX MATCHALL "CountsAsNoRoute\\(" no_route_calls "${choose}")
string(REGEX MATCHALL "lastRejects\\.crossMap \\+ lastRejects\\.zoneLevel \\+ lastRejects\\.dangerMap, lastRejects\\.turnInSuppressed,"
  route_danger_groups "${choose}")
string(REGEX MATCHALL "lastRejects\\.movedAway \\+ lastRejects\\.rangeSkip \\+ lastRejects\\.resumeSkipped,"
  unjudged_groups "${choose}")
# The local quest hub filter is no "not judged" case: a retry hides the same takers again, so the
# hub taker's own reason decides (bots 44, 310, 606, 2935 and 4512 stayed in hand-in-only in v24).
reject_text("${choose}" "+ lastRejects.hubFilter" "hub filter is not unjudged: a retry hides the same takers")
list(LENGTH no_route_calls no_route_call_count)
list(LENGTH route_danger_groups route_danger_group_count)
list(LENGTH unjudged_groups unjudged_group_count)
if(NOT no_route_call_count EQUAL 2 OR NOT route_danger_group_count EQUAL 2 OR NOT unjudged_group_count EQUAL 2)
  message(FATAL_ERROR "Expected 2 CountsAsNoRoute calls with all reject groups, found ${no_route_call_count} calls, "
    "${route_danger_group_count} route danger and ${unjudged_group_count} unjudged groups")
endif()
# The resumed choice counts what it skips; RejectCounts carries it into [QuestFirstRoute] state=rejected.
extract_between("${choose}" "if (candidateIndex++ < resumeSkip)" "if (checked++ && ai::travel_choose::OverBudget(" resume_skip
  "resume skip before the budget check")
require_text("${resume_skip}" "{\n                ++lastRejects.resumeSkipped;\n                continue;\n            }" "a skipped candidate counts as not judged")
require_text("${search_policy}" "uint32_t resumeSkipped = 0;" "resume skip counter")
require_text("${search_policy}" "\" resume_skipped=\" + std::to_string(resumeSkipped)" "resume skip in the rejection line")

# Gathering (hotfix 8.5): with the key on, only open turn-ins hold it back; 0 counts as before.
require_text("${travel_values}" "TravelTarget const* turnIns = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures ? AI_VALUE(TravelTarget*, \"travel target\") : nullptr;"
  "key 0: gathering count unchanged")
require_text("${travel_values}" "(!turnIns || turnIns->IsTurnInOpen(questId))" "gathering counts open turn-ins only (critic B1.2)")
require_order("${travel_values}" "turnIns->IsTurnInOpen(questId)" "if (ai::quest_search::GatherYieldsToTurnIn(rosterOnItsOwn, finished))" "count before the 8.5 check")

# Neutral keys: 0 = off, the old behaviour; the route danger switch off = critic B1.3.
require_text("${config_header}" "uint32 questFirstProgressionTurnInParkFailures = 0;" "default off")
require_text("${config_header}" "bool questFirstProgressionTurnInParkCountsRouteDanger = false;" "route danger switch off")
require_text("${config_source}" "std::min<uint32>(255, config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.TurnInParkFailures\", 0))" "off by default, at most the policy's 255")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.TurnInParkWindowSeconds\", 3600" "window default")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.TurnInParkSeconds\", 3600" "park default")
require_text("${config_source}" "\"AiPlayerbot.QuestFirstProgression.TurnInParkCountsRouteDanger\", false" "route danger switch off by default")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.TurnInParkFailures = 0" "documented key")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.TurnInParkWindowSeconds = 3600" "documented key")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.TurnInParkSeconds = 3600" "documented key")
require_text("${config_template}" "AiPlayerbot.QuestFirstProgression.TurnInParkCountsRouteDanger = 0" "documented key")
require_text("${config_template}" "also for every bot, not only roster bots" "the effects of the key on every bot are documented")
# The route danger switch text names all three deferrals, and a cross-map deferral is any other
# map, not only the other continent (CrossMapContinentsOnly = 0; cross check S4).
extract_between("${config_template}" "# With 1, a turn-in-only request whose takers a route danger deferral took"
  "AiPlayerbot.QuestFirstProgression.TurnInParkCountsRouteDanger = 0" route_danger_doc "route danger switch text above its key")
require_text("${route_danger_doc}" "That covers all three deferrals:" "all deferrals governed by the switch")
require_text("${route_danger_doc}" "detail=death_cluster" "death cluster deferrals governed by the switch")
require_text("${route_danger_doc}" "not only the other continent" "cross map is any other map (S4)")
reject_text("${route_danger_doc}" "another continent" "continent-only wording (S4)")

message(STATUS "QUEST_TURNIN_PARK_CONTRACT=PASS")
