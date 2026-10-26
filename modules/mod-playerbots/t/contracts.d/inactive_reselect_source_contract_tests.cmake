cmake_policy(VERSION 3.16)
# twow-repo#601 cr2-C gap analysis (CLI-Deep 25.10.2026, OB-00 go): AiPlayerbot.QuestLoop.InactiveReselect (default 0) - an inactive
# objective without new credit (an emptied camp) is remembered for its QL-2 cooldown in a fixed ring of 4 per bot and the target
# ends, so the next search runs at once; the search and the refresh skip the camp while it is remembered. The search right after it
# finding no quest target puts the bot back into the old COOLDOWN for the time left (fail closed). Pins the key, the ring, the
# inactive drop, the skip, the refresh, the fallback and the rejected line.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "inactive reselect: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "inactive reselect: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "inactive reselect: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "inactive reselect: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "inactive reselect: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)
read_source("TravelMgr.h" travel_h)
read_source("QuestLoopPolicy.h" policy)
read_source("QuestSearchPolicy.h" search_policy)
read_source("QuestFailureClassPolicy.h" class_policy)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 questLoopInactiveReselect = 0;" "member default off")
require_text("${config_cpp}" "questLoopInactiveReselect = config.GetIntDefault(\"AiPlayerbot.QuestLoop.InactiveReselect\", 0);" "default 0")
require_text("${config_cpp}" "if (questLoopInactiveReselect > 2)" "unknown mode checked")
require_text("${config_cpp}" "questLoopInactiveReselect = 0;\n    }" "unknown mode is off")
require_text("${config_cpp}" "{\"QuestLoop.InactiveReselect\", Value(questLoopInactiveReselect)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.QuestLoop.InactiveReselect = 0\n" "documented")

# 2. The ring: 4 slots per bot, own slot first, then a run-out one, then the one running out first; a wrapped end is not free.
require_text("${policy}" "constexpr std::size_t EmptyCampSlots = 4;" "fixed ring of 4")
require_text("${policy}" "EmptyCamp slots[EmptyCampSlots];" "no container that grows")
require_text("${policy}" "return camp.untilMs && std::int32_t(camp.untilMs - nowMs) > 0;" "wrap-safe live test")
require_order("${policy}" "if (camp.quest == quest && camp.entry == entry)\n                slot = &camp;" "if (!slot && !CampMarkLive(camp, nowMs))" "own slot before a run-out one")
require_text("${policy}" "slot->untilMs = 1;   // a wrapped clock must not read as \"free\"" "wrapped end kept")
require_text("${travel_h}" "quest_loop::EmptyCamps emptyCamps;" "ring per travel target")
require_text("${travel_h}" "bool inactiveReselectPending = false;" "no fallback by default")

# 3. The inactive drop: only in the #581 emptied-camp branch (after QL-1 / QL-2, never for progress), roster bot on its own, cohort
#    mode; the mark lasts the cooldown just set; the target ends (EXPIRED) after the mark and the line; the request value is cleared.
between("${travel}" "else if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" "            if (progressRule)\n" camp)
require_order("${camp}" "NoteQuestObjectiveFailure();" "ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveReselect, bot->GetGUIDLow()) &&" "QL-1 counts first")
require_text("${camp}" "ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveReselect, bot->GetGUIDLow()) &&\n                    sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster())" "roster bot on its own, cohort")
require_text("${camp}" "uint32 const markMs = quest_loop::InactiveCooldownMs(tDestination->GetCooldownDelay(), sPlayerbotAIConfig.questLoopInactiveCooldownSeconds);" "mark = the QL-2 cooldown")
require_text("${camp}" "\"[QuestLoop] state=inactive_reselect bot=%u level=%u quest=%u target_entry=%d mark_ms=%u\"" "greppable line")
require_order("${camp}" "quest_loop::MarkEmptyCamp(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime(), markMs);" "SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "marked before the target ends")
require_order("${camp}" "SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "inactiveReselectPending = true;" "fallback armed for this target")
require_text("${camp}" "ai->GetAiObjectContext()->ClearValues(\"no active travel destinations\");" "next request not blocked")
between("${travel}" "TraceQuestCommit(tDestination, \"drop\", destinationInactive ? \"destination_inactive\" : \"conditions_inactive\");" "else if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" progress)
string(FIND "${progress}" "questLoopInactiveReselect" in_progress)
if(NOT in_progress EQUAL -1)
  message(FATAL_ERROR "inactive reselect: the progress branch (InactiveAfterProgress) must stay without it")
endif()
between("${travel}" "void TravelTarget::SetTarget(" "\n}\n" set_target)
require_text("${set_target}" "if (tDestination1 != tDestination)\n        inactiveReselectPending = false;" "a new destination drops the fallback")

# 4. The search: an objective in a remembered camp is skipped (after QL-1, before the map rules), counted, never chosen.
between("${choose}" "CandidateSkip CandidateSkipOf(" "\n}\n" skip_of)
require_order("${skip_of}" "return CandidateSkip::QuestLoopSuppressed;" "if (persistentTarget && persistentTarget->IsInactiveReselectMarked(destination))\n        return CandidateSkip::InactiveReselect;" "after QL-1")
require_order("${skip_of}" "return CandidateSkip::InactiveReselect;" "return CandidateSkip::OtherMapGather;" "before the map rules")
require_text("${choose}" "case CandidateSkip::InactiveReselect:\n                    ai->TellDebug(requester, \"Skipping quest objective whose camp was just found empty.\", \"debug travel\");\n                    ++lastRejects.inactiveReselect;\n                    continue;" "counted and skipped")
require_text("${travel}" "return objective && quest_loop::EmptyCampLeftMs(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime()) > 0;" "objectives only, while the mark lasts")

# 5. The refresh brings no remembered camp back.
between("${choose}" "bool RefreshTravelTargetAction::Execute(Event& event)" "\n}\n" refresh)
require_text("${refresh}" "if (target->IsInactiveReselectMarked(oldDestination))\n    {\n        ai->TellDebug(requester, \"Old destination is a camp just found empty.\", \"debug travel\");\n        return false;\n    }" "refresh skips the camp")
require_order("${refresh}" "if (target->IsInactiveReselectMarked(oldDestination))" "if (!target->IsDestinationActive())" "before the old destination is taken up again")

# 6. Fail closed: no quest target after the emptied camp -> the old COOLDOWN for the time left, before any empty-search count. The
#    choice takes the pending flag once (any purpose, also an invalid list) BEFORE it sets the status to NONE - the status can not
#    tell it any more (pre-review 26.10.: a check on EXPIRED never held there).
require_text("${travel_h}" "bool TakeInactiveReselectPending() { bool const pending = inactiveReselectPending; inactiveReselectPending = false; return pending; }" "taken once")
between("${choose}" "bool ChooseTravelTargetAction::Execute(Event& event)" "if (!SetBestTarget(requester, &newTarget, destinationList, true, travelTarget))" execute_head)
require_text("${execute_head}" "travelTarget->TakeInactiveReselectPending();   // twow-repo#601 (QuestLoop.InactiveReselect): no stale fallback\n        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);" "an invalid list drops the flag")
require_text("${execute_head}" "bool const reselectPending = travelTarget->TakeInactiveReselectPending();\n    travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);" "taken before the status goes to NONE")
between("${choose}" "if (!SetBestTarget(requester, &newTarget, destinationList, true, travelTarget))" "SET_AI_VALUE2(bool, \"no active travel destinations\", futureTravelPurpose, true);" no_target)
require_order("${no_target}" "if (chooseBudgetExceeded)\n            return false;" "if (reselectPending && futureTravelPurpose == \"quest\" && travelTarget->FallBackToInactiveCooldown())" "quest choice right after the camp only")
require_text("${no_target}" "\"inactive_reselect_fallback\", lastRejects.Format()" "rejected line names the fallback")
require_text("${no_target}" "lastRejects.Format() + ai::contested_objective::FormatSources(lastSuppressSources));\n            return false;\n        }" "returns before widening")
between("${travel}" "bool TravelTarget::FallBackToInactiveCooldown()" "\n}\n" fallback)
require_text("${fallback}" "if (!objective || !bot)\n        return false;" "the camp is still the destination")
require_order("${fallback}" "if (!leftMs)\n        return false;" "SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" "only while the mark lasts")
require_order("${fallback}" "SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" "SetExpireIn(leftMs);" "the time left on the mark")
require_text("${fallback}" "\"[QuestLoop] state=inactive_reselect_fallback bot=%u level=%u quest=%u target_entry=%d cooldown_ms=%u\"" "greppable line")

# 7. The rejected line: the skip count only when counted (every line without the switch unchanged); a transient reason.
require_text("${search_policy}" "(inactiveReselect ? \" inactive_reselect=\" + std::to_string(inactiveReselect) : std::string()) + FormatInactiveReasons();" "token only when counted")
require_text("${class_policy}" "rejects.resumeSkipped + rejects.inactiveReselect;" "classified transient")
