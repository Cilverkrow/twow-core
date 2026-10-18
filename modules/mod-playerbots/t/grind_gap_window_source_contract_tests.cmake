cmake_policy(VERSION 3.16)
# twow-repo#601 S2 / S4 (OB-10 rev. 2, 18.10.2026): AiPlayerbot.QuestFirstProgression.GrindMode = 1, the gap filler - the
# GrindMode 2 population grinds only while its quest route backoff runs (R1) and the grind trip ends with that backoff (R2).
# Pins:
#  - the pure policy (GapFiller, the population, R1 GapWindowOpen, R2 GapWindowSeconds / GapStatusMs, the window line),
#  - every new branch asks GrindMode 1 first (IsQuestGapGrind: the config before anything else),
#  - R1 in NeedTravelPurposeValue(Grind): after S1's turn-in yield, before the S0 GrindFallbackMinutes, FreshGrindFailures
#    and 8.32 GrindFallbackFailures lines, an unconditional return (S0 and the gap filler never both), any failure class
#    (OP4), no refresh renewal,
#  - no money grind for the population outside the R1 window: the grind request only inside it, whichever trigger queued it,
#    and the gap trip's condition is R1 itself, never the money condition (recheck of review B-F3, OB-10 18.10.2026), so a
#    bot that "should get money" grinds only inside the window and never longer,
#  - R2: the gap grind trip's READY / TRAVEL / WORK status time ends with the window; its expiry is no failure (no work
#    timeout, no turn-in / quest loop / move retry / destination death count, no quest route counter); review fixes: an
#    inactive destination / condition ends the trip at once (EXPIRED, expired= at a condition end, no COOLDOWN, C-F1 / B-F1),
#    a COOLDOWN never outlives the window, no refresh of a gap trip (B-F5),
#  - one [QuestSearch] state=grind_window line per window, state=grind_fallback stays GrindMode 0,
#  - commit 2 (OB-10 rev. 2 points 1, 2, 5): R3 the level window [max(gray + 1, L - 5), L + 1] for grind destinations
#    (template MaxLevel in IsPossible, area level in IsLocationLevelValid) only through a scope bit of the PlayerTravelInfo
#    snapshot set on the bot thread, never computed in the async job; own map only (CandidateSkip::OtherMapGrind inside
#    CandidateSkipOf, still 2 call sites); R4 plain grind candidates of the population in the window, attackers / victims /
#    quest mobs stay; R5 the isGrindTravelDest fix under mode 1 only; [GrindWindow] minute line, mode 1 alone, omitted
#    when every count is 0; OB-10 18.10.2026: r4_empty= (grind target passes in which R4 skipped a candidate and no target
#    came out), one relaxed add per such pass after the candidate loop, part of the all-zero omission; recheck (fix 3):
#    counted once per value pass in Calculate after every assist round (round 0's skips, a later round's target is no
#    empty pass); engaged= / grey_engaged= only while the quest route backoff still runs (R1 open); the mode-1 conf text
#    pinned (the money grind only inside the window),
#  - review fixes (C-F2, C-F3 / B-F2): the R3 window snapshot and the own-map skip only for the gap grind fetch, its choice
#    (TravelTarget flag set only behind IsQuestGapGrind, never copied) and the gap trip - a quest objective fetch, choice or
#    trip to grind mobs keeps the old tests; no group copy of a grind trip for the population,
#  - no new config key, no lock.
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
    message(FATAL_ERROR "#601 S2/S4 gap window: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 S2/S4 gap window: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#601 S2/S4 gap window: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `start` up to the first `end` after it (both required).
function(region text start end out)
  string(FIND "${text}" "${start}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "#601 S2/S4 gap window: missing region start: ${start}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(LENGTH "${start}" start_length)
  string(SUBSTRING "${rest}" ${start_length} -1 tail)
  string(FIND "${tail}" "${end}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#601 S2/S4 gap window: missing region end: ${end}")
  endif()
  math(EXPR length "${start_length} + ${end_at}")
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR next "${at} + ${len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#601 S2/S4 gap window: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("QuestGrindModePolicy.h" policy)
read_source("QuestSearchPolicy.h" search_policy)
read_source("QuestFailureClassPolicy.h" failure_policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_mgr)
read_source("strategy/values/TravelValues.cpp" values)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/generic/TravelStrategy.cpp" travel_strategy)
read_source("strategy/values/GrindTargetValue.cpp" grind_target)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_target)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("DirectTargetDestinations.cpp" direct_targets)

# ---- 1. The pure policy ------------------------------------------------------------------------------
require_text("${policy}" "inline bool GapFiller(std::uint32_t grindMode)\n{\n    return grindMode == ModeGapFiller;\n}" "mode 1 test")
require_text("${policy}" "    return GapFiller(grindMode) && questFirst && !realMaster && (!maxLevel || level < maxLevel);" "the GrindMode 2 population, under mode 1")
require_text("${policy}" "    return NoGrind(grindMode) && questFirst && !realMaster && (!maxLevel || level < maxLevel);" "mode 2 population unchanged (the same bots)")
require_text("${policy}" "inline bool GapWindowOpen(bool routeBackoffRunning, bool travelRefresh)\n{\n    return routeBackoffRunning && !travelRefresh;\n}" "R1: only inside the backoff, never a refresh renewal")
require_text("${policy}" "constexpr std::uint32_t GapWindowGraceSeconds = 30;" "R2: + 30 s")
require_text("${policy}" "    std::uint32_t const remaining = backoffUntil > now ? backoffUntil - now : 0u;\n    return (remaining < maxBackoffSeconds ? remaining : maxBackoffSeconds) + GapWindowGraceSeconds;" "R2: remaining backoff, at most the longest backoff, + 30 s")
require_text("${policy}" "    if (!windowEnd)\n        return statusMs;" "R2: no window, status time unchanged (every other target)")
require_text("${policy}" "    return left ? (left < longest ? left : longest) * 1000u : 1u;" "R2: the time left, never longer than the longest window, expired at once after it")
require_text("${policy}" "    return \"[QuestSearch] state=grind_window bot=\" + std::to_string(bot) + \" level=\" + std::to_string(level) + \" backoff_s=\" +\n        std::to_string(backoffSeconds) + \" window=\" + std::to_string(lowLevel) + \"-\" + std::to_string(highLevel) + \" map=\" +\n        std::to_string(map);" "grind_window line format")
require_text("${search_policy}" "constexpr uint32_t RouteBackoffMaxSeconds = 600;" "the longest backoff the window is bounded by")
# S1's yield never holds inside the backoff - that is why R1 needs no turn-in count of its own.
require_text("${failure_policy}" "    return rosterOnItsOwn && routableOpenTurnIns > 0 && !routeBackoffRunning;" "S1 yield unchanged")

# ---- 2. GrindMode 1 first ---------------------------------------------------------------------------
require_text("${ai_h}" "    bool IsQuestGapGrind();" "declared")
region("${ai_cpp}" "bool PlayerbotAI::IsQuestGapGrind()" "\n}\n" gap_fn)
require_order("${gap_fn}" "if (!ai::quest_grind_mode::GapFiller(sPlayerbotAIConfig.questFirstProgressionGrindMode) || !bot)\n        return false;" "sRandomPlayerbotMgr.IsPersistentRosterMember" "GrindMode 0 / 2 return before any other test")
require_text("${gap_fn}" "return ai::quest_grind_mode::GapFillerApplies(sPlayerbotAIConfig.questFirstProgressionGrindMode,\n        sPlayerbotAIConfig.questFirstProgressionEnabled && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()),\n        HasRealPlayerMaster(), bot->GetLevel(), sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));" "the IsQuestNoGrind population")

# ---- 3. R1: NeedTravelPurposeValue(Grind) ------------------------------------------------------------
region("${values}" "    case TravelDestinationPurpose::Grind:\n    {" "        uint32 rpgPhase = ai->GetFixedBotNumber" grind_case)
require_order("${grind_case}" "if (ai::quest_failure_class::GrindYieldsToTurnIn(true, routableOpen, routeBackoffRunning))\n                    return false;" "if (ai->IsQuestGapGrind())" "S1's turn-in yield first")
foreach(s0_line
    "uint32 const failures = uint32(std::max(0, AI_VALUE2(int, \"manual int\", \"quest route failures\")));"
    "if (ai->IsQuestNoGrind())"
    "if (!ai::quest_search::GrindFallbackFresh(sPlayerbotAIConfig.questFirstProgressionGrindFallbackMinutes,"
    "return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.directTargetsFreshGrindFailures) &&"
    "return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures) &&")
  require_order("${grind_case}" "if (ai->IsQuestGapGrind())" "${s0_line}" "R1 returns before the S0 / P3-a / 8.32 lines")
endforeach()
# Never both: the mode-1 test is followed by an unconditional return.
require_text("${grind_case}" "            if (ai->IsQuestGapGrind())\n                return ai::quest_grind_mode::GapWindowOpen(\n                    uint32(time(nullptr)) < uint32(std::max(0, AI_VALUE2(int, \"manual int\", \"quest route backoff until\"))),\n                    AI_VALUE2(bool, \"manual bool\", \"is travel refresh\"));" "R1: the backoff test of the quest request, no refresh renewal, always a return")
region("${grind_case}" "if (ai->IsQuestGapGrind())" "            // Hotfix 8.32" r1_block)
# OP4: any failure class opens a window (a transient backoff too) - no class, counter or S0 stamp in R1.
foreach(banned "quest route failures" "quest route failed at" "GrindFallback" "FreshBot" "CountsTowardGrind" "failureClass" "Transient" "NoteQuestIdle" "SET_AI_VALUE")
  forbid_text("${r1_block}" "${banned}" "R1 reads only the backoff (OP4) and writes nothing")
endforeach()
count_text("${values}" "ai->IsQuestGapGrind()" 1 "one R1 site")

# ---- 4. No money grind for the population ------------------------------------------------------------
require_text("${travel_strategy}" "{\"val::should get money\", TravelDestinationPurpose::Grind, 6.77f}," "the money trigger itself unchanged (other bots)")
region("${choose}" "bool RequestTravelTargetAction::isUseful() {" "\n}\n" request_useful)
require_text("${request_useful}" "    if (ai->IsQuestGapGrind() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)) &&\n        !AI_VALUE2(bool, \"need travel purpose\", getQualifier()))\n        return false;" "a grind fetch only inside the R1 window, whichever trigger")
require_order("${request_useful}" "if (ai->IsQuestNoGrind() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)))" "if (ai->IsQuestGapGrind() && getQualifier()" "after the mode-2 refusal")
require_order("${request_useful}" "if (ai->IsQuestGapGrind() && getQualifier()" "if (!isAllowed())" "before the skip chance")
# Recheck of review B-F3 (deliberate pin change, OB-10 18.10.2026): the money trigger (6.77) and the plain trigger (6.35)
# queue the same action "request travel target::<Grind>", and Queue::Push merges baskets of one action name and keeps the
# event of the higher relevance - the merged basket carries the money event. The former refusal of the money event in
# Execute (GapGrindRequestAllowed) therefore dropped the R1 request as well, every tick: a bot that "should get money" never
# started a gap trip. Now: no refusal in Execute (isUseful above already stops the grind request outside the R1 window,
# whichever trigger), and the gap trip's stored condition ("future travel condition", SetConditions in setNewTarget) is R1
# itself, never the money condition - so the trip ends with the window (the inactive branch below, C-F1) and a money
# condition never holds it longer (OB-10 18.10.).
require_text("${policy}" "inline std::string GapTripCondition(bool gapGrind, std::string const& eventSource, std::uint32_t grindPurpose)\n{\n    return gapGrind ? \"need travel purpose::\" + std::to_string(grindPurpose) : eventSource;\n}" "the gap trip's condition is R1, every other request keeps its event source")
forbid_text("${policy}" "GapGrindRequestAllowed" "the money refusal helper (it dropped the merged R1 request)")
# The condition is the R1 value itself: the same value name the plain request trigger reads, and setNewTarget stores it.
require_text("${travel_strategy}" "std::string trigger = condition.empty() ? \"val::need travel purpose::\" + std::to_string((uint32)purpose) : condition;" "R1 is the need-travel-purpose value of the plain trigger")
require_text("${choose}" "    if(!AI_VALUE2(std::string, \"manual string\", \"future travel condition\").empty())\n        AI_VALUE(TravelTarget*, \"travel target\")->SetConditions({ AI_VALUE2(std::string, \"manual string\", \"future travel condition\")});" "the stored condition becomes the trip's condition")
region("${choose}" "bool RequestTravelTargetAction::Execute(Event& event)\n{" "    return true;\n}" request_exec)
require_text("${request_exec}" "    bool const gapGrindRequest = ai->IsQuestGapGrind() && actionPurpose == TravelDestinationPurpose::Grind;" "the gap grind request (GrindMode 1 first)")
require_order("${request_exec}" "if (IsAutonomousRosterTravelBlocked(ai, actionPurpose) && !event.getOwner())" "bool const gapGrindRequest =" "after the roster travel block")
require_text("${request_exec}" "    SET_AI_VALUE2(std::string, \"manual string\", \"future travel condition\",\n        ai::quest_grind_mode::GapTripCondition(gapGrindRequest, event.getSource(), uint32(TravelDestinationPurpose::Grind)));" "the gap trip stores R1 as its condition, never the money condition")
forbid_text("${request_exec}" "\"future travel condition\", event.getSource());" "the raw event source stored for a gap trip (the money condition would outlive the window)")
# No refusal of the money source in Execute (it would drop the merged R1 request): no money test at all, and no early
# return between the gap request and the fetch.
foreach(banned "IsMoneyCondition" "should get money" "GapGrindRequestAllowed")
  forbid_text("${request_exec}" "${banned}" "a money refusal in Execute (Queue::Push merged the R1 request into the money basket)")
endforeach()
region("${request_exec}" "bool const gapGrindRequest =" "std::async(std::launch::async," gap_request)
forbid_text("${gap_request}" "return false" "a refusal of the gap grind request after the window test (isUseful)")
count_text("${choose}" "GapTripCondition(" 1 "one gap trip condition site")
count_text("${choose}" "GapGrindRequestAllowed(" 0 "no money refusal site")

# ---- 5. The gap grind trip (ChooseTravelTargetAction) ------------------------------------------------
region("${choose}" "else if (futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)) && UsesQuestFirstProgression(bot))\n    {" "    setNewTarget(requester, &newTarget, travelTarget);" grind_branch)
require_order("${grind_branch}" "if (ai->IsQuestNoGrind())" "if (ai->IsQuestGapGrind())" "mode 2 refusal first")
require_order("${grind_branch}" "if (ai->IsQuestGapGrind())" "[QuestSearch] state=grind_fallback bot=%u level=%u failures=%d" "the gap trip before the mode-0 line")
region("${grind_branch}" "if (ai->IsQuestGapGrind())" "        else\n        {\n            // Hotfix 8.32" gap_block)
require_order("${gap_block}" "if (!ai::quest_grind_mode::GapWindowOpen(now < uint32(backoffUntil), false))\n                return false;" "newTarget.SetGapWindowEnd(" "R1 again before the trip (the fetch ran async)")
require_text("${gap_block}" "newTarget.SetGapWindowEnd(now + ai::quest_grind_mode::GapWindowSeconds(uint32(backoffUntil), now,\n                ai::quest_search::RouteBackoffMaxSeconds));" "R2: the window end from the backoff")
require_text("${gap_block}" "if (backoffUntil != AI_VALUE2(int, \"manual int\", \"gap window logged\"))\n            {\n                SET_AI_VALUE2(int, \"manual int\", \"gap window logged\", backoffUntil);" "one window line per backoff")
require_text("${gap_block}" "sLog.outBasic(\"%s\", ai::quest_grind_mode::FormatGapWindowLine(" "the window line from the policy format")
require_text("${gap_block}" "ai::quest_grind_mode::GapWindowLowLevel(level, MaNGOS::XP::GetGrayLevel(level))" "window low level from the grey level")
foreach(banned "\"quest route failures\"" "\"quest route failed at\"" "SET_AI_VALUE2(int, \"manual int\", \"quest route backoff until\"" "NoteTurnInFailure" "NoteQuestIdle" "state=grind_fallback" "std::mutex")
  forbid_text("${gap_block}" "${banned}" "the gap trip writes no failure counter, no idle note, no fallback line, no lock")
endforeach()
require_text("${grind_branch}" "        else\n        {\n            // Hotfix 8.32 (twow-repo#544): a quest-first bot grinds after empty quest route searches.\n            sLog.outBasic(\"[QuestSearch] state=grind_fallback" "state=grind_fallback only without the gap filler (GrindMode 0)")
forbid_text("${choose}" "\"[QuestSearch] state=grind_window" "a grind_window line outside the policy format")
count_text("${choose}" "FormatGapWindowLine(" 1 "one window line site")
count_text("${choose}" "SetGapWindowEnd(" 1 "one window end site")
count_text("${choose}" "ai->IsQuestGapGrind()" 6 "the gap trip, the request; commit 2: no_dest=; review fixes: the gap grind choice flag (own map reads it), no group grind copy, the gap request (R1 condition)")
foreach(src values travel_mgr ai_cpp)
  forbid_text("${${src}}" "SetGapWindowEnd(" "a window end set elsewhere (${src})")
endforeach()

# ---- 6. R2: status time and a silent expiry (TravelTarget) --------------------------------------------
require_text("${travel_h}" "uint32 gapWindowEnd = 0;" "0 for every target but a gap grind trip")
require_text("${travel_h}" "void SetGapWindowEnd(uint32 windowEnd) { gapWindowEnd = windowEnd; }" "setter")
region("${travel_mgr}" "void TravelTarget::SetStatus(TravelStatus status) {" "\n}\n" set_status)
require_order("${set_status}" "statusTime = tDestination->GetCooldownDelay();" "if (gapWindowEnd && (m_status == TravelStatus::TRAVEL_STATUS_READY || m_status == TravelStatus::TRAVEL_STATUS_TRAVEL ||\n            m_status == TravelStatus::TRAVEL_STATUS_WORK))" "the cap after the normal status times, READY / TRAVEL / WORK")
require_text("${set_status}" "statusTime = ai::quest_grind_mode::GapStatusMs(statusTime, gapWindowEnd, uint32(time(nullptr)),\n            ai::quest_search::RouteBackoffMaxSeconds);" "R2 status time from the policy")
region("${travel_mgr}" "void TravelTarget::SetTarget(TravelDestination* tDestination1, WorldPosition* wPosition1) {" "\n}\n" set_target)
require_order("${set_target}" "    if (tDestination1 != tDestination)\n        gapWindowEnd = 0;" "    tDestination = tDestination1;" "another destination never inherits a window (the refresh keeps it: no renewal)")
region("${travel_mgr}" "bool TravelTarget::CopyTarget(TravelTarget* const target) {" "\n}\n" copy_target)
require_order("${copy_target}" "SetTarget(target->tDestination, target->wPosition);" "gapWindowEnd = target->gapWindowEnd;" "the choice's window after SetTarget's clear")
count_text("${travel_mgr}" "gapWindowEnd = " 2 "window end written only in SetTarget (clear) and CopyTarget")
region("${travel_mgr}" "    if (statusTime != 0 && GetTimeLeft() <= 0 && !IsForced())\n    {" "        return;\n    }" expiry)
require_text("${expiry}" "        if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK && !gapWindowEnd)\n            OnWorkTimeout();" "the window end is no work timeout")
foreach(banned "NoteTurnInFailure" "NoteQuestObjectiveFailure" "OnMoveRetryCooldown" "OnDeathAtDestination" "OnDeathOnTurnInRoute" "SuppressCurrentDestination" "ParkTurnInNow" "quest route failures" "IncRetry")
  forbid_text("${expiry}" "${banned}" "the status time expiry counts no failure (OB-10 rev. 2 point 3)")
endforeach()
# Review C-F1 / B-F1: the plain gap trip's condition is R1 itself ("need travel purpose::Grind"), false at the backoff end
# before the window end - the inactive branch ends a gap trip at once (EXPIRED, no COOLDOWN, no failure), expired= at a
# condition end; an inactive destination ends silently. The quest-loop cooldown / failure below stays for other targets.
region("${travel_mgr}" "        if (destinationInactive || conditionsInactive)\n        {" "            return;\n        }\n    }\n}" inactive)
set(gap_inactive "            if (gapWindowEnd)\n            {\n                ai->TellDebug(ai->GetMaster(), \"The gap grind trip ends because its destination or its window is no longer active.\", \"debug travel\");\n                if (conditionsInactive)\n                    PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));\n                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);\n                ai->GetAiObjectContext()->ClearValues(\"no active travel destinations\");\n                return;\n            }")
require_text("${inactive}" "${gap_inactive}" "a gap trip ends at once, expired= only at a condition end")
require_order("${inactive}" "if (gapWindowEnd)\n            {" "SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" "before the COOLDOWN")
require_order("${inactive}" "if (gapWindowEnd)\n            {" "NoteQuestObjectiveFailure();" "before the quest loop failure")
require_order("${inactive}" "if (gapWindowEnd)\n            {" "TraceQuestCommit(tDestination, \"drop\"" "before the quest commit drop trace (the quest loop region stays as it was)")
region("${inactive}" "            if (gapWindowEnd)\n            {" "                return;\n            }" gap_inactive_block)
foreach(banned "NoteTurnInFailure" "NoteQuestObjectiveFailure" "OnMoveRetryCooldown" "OnDeathAtDestination" "OnWorkTimeout" "SetExpireIn" "quest route failures" "IncRetry" "COOLDOWN")
  forbid_text("${gap_inactive_block}" "${banned}" "the inactive end of a gap trip counts no failure (OB-10 rev. 2 point 3)")
endforeach()
# Review C-F1 / B-F1: a COOLDOWN of a gap trip (move retries, a group member's check) never outlives the window.
require_text("${policy}" "    std::uint32_t const left = GapStatusMs(statusMs, windowEnd, now, maxBackoffSeconds);\n    return statusMs && statusMs < left ? statusMs : left;" "the shorter of the cooldown and the window")
require_text("${set_status}" "    else if (gapWindowEnd && m_status == TravelStatus::TRAVEL_STATUS_COOLDOWN)\n        statusTime = ai::quest_grind_mode::GapCooldownMs(statusTime, gapWindowEnd, uint32(time(nullptr)),\n            ai::quest_search::RouteBackoffMaxSeconds);" "the COOLDOWN cap of a gap trip")
# Review B-F5: no refresh of a gap trip, in or after its window (no renewal, no second expired=), before the skip chance.
region("${choose}" "bool RefreshTravelTargetAction::isUseful()\n{" "\n}\n" refresh_useful)
require_text("${refresh_useful}" "    if (AI_VALUE(TravelTarget*, \"travel target\")->GetGapWindowEnd())\n        return false;" "no refresh of a gap trip")
require_order("${refresh_useful}" "->GetGapWindowEnd())\n        return false;" "if (urand(1, 100) <= 10)" "before the skip chance")

# ---- 7. No new key, default 0 ------------------------------------------------------------------------
foreach(src config_h config_cpp conf)
  forbid_text("${${src}}" "GapWindow" "a new config key (${src})")
  forbid_text("${${src}}" "GrindGap" "a new config key (${src})")
endforeach()
require_text("${conf}" "\nAiPlayerbot.QuestFirstProgression.GrindMode = 0\n" "GrindMode default 0")
forbid_text("${conf}" "AiPlayerbot.QuestFirstProgression.GrindMode = 1" "GrindMode shipped as 1")

# ==== Commit 2: R3 window, own map, R4, R5, [GrindWindow] (OB-10 rev. 2 points 1, 2, 5) =========================

# ---- 8. The pure window rules ------------------------------------------------------------------------
require_text("${policy}" "inline bool InGapWindow(std::uint32_t lowLevel, std::uint32_t highLevel, std::uint32_t level)\n{\n    return highLevel && level >= lowLevel && level <= highLevel;\n}" "R3 / R4: inside [low, high], no window fails closed")
require_text("${policy}" "    std::uint32_t const belowLevel = level > 5u ? level - 5u : 1u;\n    return grayLevel + 1u > belowLevel ? grayLevel + 1u : belowLevel;" "window low max(gray + 1, L - 5), no wrap")
require_text("${policy}" "inline std::uint32_t GapWindowHighLevel(std::uint32_t level)\n{\n    return level + 1u;\n}" "window high L + 1")
require_text("${policy}" "inline bool GapGrindStaysOnMap(bool gapGrind, bool grindDestination, std::uint32_t botMap, std::uint32_t targetMap)\n{\n    return !gapGrind || !grindDestination || botMap == targetMap;\n}" "own map only for a mode-1 grind destination")
require_text("${policy}" "    return gapGrind && !neededForQuest && !victimIsBot && !InGapWindow(lowLevel, highLevel, creatureLevel);" "R4: plain candidates only (quest mobs and creatures on the bot stay)")
require_text("${policy}" "    return windowOpen && backoffUntil != startedFor && backoffUntil != noDestFor;" "no_dest once per window, not after its trip started")
require_text("${policy}" "    for (std::uint32_t const count : counts)\n        any = any || count != 0;\n    if (!any)\n        return std::string();" "minute line omitted when every count is 0 (all slots, r4_empty= too)")
require_text("${policy}" "    return \"[GrindWindow] minute starts=\" + std::to_string(counts[std::size_t(WindowEvent::Starts)]) +\n        \" expired=\" + std::to_string(counts[std::size_t(WindowEvent::Expired)]) +\n        \" engaged=\" + std::to_string(counts[std::size_t(WindowEvent::Engaged)]) +\n        \" grey_engaged=\" + std::to_string(counts[std::size_t(WindowEvent::GreyEngaged)]) +\n        \" no_dest=\" + std::to_string(counts[std::size_t(WindowEvent::NoDest)]) +\n        \" r4_skips=\" + std::to_string(counts[std::size_t(WindowEvent::R4Skips)]) +\n        \" r4_empty=\" + std::to_string(counts[std::size_t(WindowEvent::R4Empty)]);" "[GrindWindow] line format (OB-10 18.10.: r4_empty= last)")
require_text("${policy}" "constexpr std::size_t WindowEventCount = 7;" "seven counters (OB-10 18.10.: r4_empty=)")
require_text("${policy}" "    R4Skips,      // plain grind candidates R4 skipped (per grind target pass, not per engagement)\n    R4Empty,      // grind target passes in which R4 skipped a candidate and no target came out (OB-10 18.10.2026)\n};" "r4_empty= the last slot, right after r4_skips=")
# OB-10 18.10.2026: r4_empty= counts a pass only when R4 skipped at least one candidate and no target came out.
require_text("${policy}" "inline bool GapR4EmptyPass(std::uint32_t skipped, bool targetFound)\n{\n    return skipped != 0 && !targetFound;\n}" "r4_empty=: skipped > 0 and no target")

# ---- 9. R3 for the scope only: a value of the snapshot, set on the bot thread (OB-10 rev. 2 point 1) -----------
require_text("${travel_h}" "bool HasGapWindow() const { return gapWindowHigh != 0; }" "the scope bit")
require_text("${travel_h}" "uint32 gapWindowLow = 0;" "no window by default (low)")
require_text("${travel_h}" "uint32 gapWindowHigh = 0;" "no window by default (high)")
forbid_text("${travel_h}" "SetGapWindowLow" "a setter (the snapshot is written only by its constructor)")
forbid_text("${travel_h}" "SetGapWindowHigh" "a setter (the snapshot is written only by its constructor)")
region("${travel_mgr}" "PlayerTravelInfo::PlayerTravelInfo(Player* player, bool gapGrind)\n{" "\n}\n" info_ctor)
require_text("${info_ctor}" "    if (ai->IsQuestGapGrind() && gapGrind)\n    {\n        gapWindowLow = ai::quest_grind_mode::GapWindowLowLevel(level, MaNGOS::XP::GetGrayLevel(level));\n        gapWindowHigh = ai::quest_grind_mode::GapWindowHighLevel(level);\n    }" "the scope bit from IsQuestGapGrind (GrindMode 1 first) on the bot thread, gap grind snapshots only")
# Review C-F2: no window unless asked for a gap grind - the default snapshot (every PlayerTravelInfo(bot)) keeps the old
# template test and area gate, so a quest objective fetch / choice / trip to grind mobs is unchanged. Asked only by the grind
# fetch, the gap grind choice (SetBestTarget), the gap trip (IsDestinationActive) and a member's gap trip (group check).
require_text("${travel_h}" "PlayerTravelInfo(Player* player, bool gapGrind = false);" "no window by default")
require_text("${travel_mgr}" "    return tDestination->IsActive(player, PlayerTravelInfo(player, gapWindowEnd != 0));" "the gap trip judged with its window, every other trip without")
require_text("${choose}" "[partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot, actionPurpose == TravelDestinationPurpose::Grind), center, purpose = actionPurpose," "the window snapshot only for the grind fetch")
require_text("${choose}" "        if (!groupTarget->GetDestination()->IsActive(player, PlayerTravelInfo(player, groupTarget->GetGapWindowEnd() != 0)) || !groupTarget->IsConditionsActive())" "a member's gap trip judged with its window")
region("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\nstd::vector<std::string> split(" best0)
require_text("${best0}" "    bool const gapGrindChoice = target->IsGapGrindChoice();\n    if (gapGrindChoice)\n        choiceTravelInfo.emplace(bot, true);" "the gap grind choice: one window snapshot per choice")
require_order("${best0}" "auto const reuseActive = [this, &isActive, &choiceInfo]" "choiceTravelInfo.emplace(bot, true);" "the window snapshot before any IsActive of the choice")
count_text("${choose}" "PlayerTravelInfo(bot, true)" 0 "a window snapshot built outside the gap grind choice")
count_text("${choose}" "emplace(bot, true)" 1 "one window snapshot in the choice")
require_text("${travel_h}" "void SetGapGrindChoice(bool gapGrindChoice1) { gapGrindChoice = gapGrindChoice1; }" "the choice flag setter")
require_text("${travel_h}" "bool gapGrindChoice = false;" "no gap grind choice by default")
require_text("${choose}" "    if (ai->IsQuestGapGrind() && futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)))\n        newTarget.SetGapGrindChoice(true);\n\n    if (!SetBestTarget(requester, &newTarget, destinationList, true, travelTarget))" "the gap grind choice flag only behind GrindMode 1, the grind fetch, right before the choice")
count_text("${choose}" "SetGapGrindChoice(" 1 "one choice flag site")
foreach(src values travel_mgr ai_cpp grind_target choose_target)
  forbid_text("${${src}}" "SetGapGrindChoice(" "a choice flag set elsewhere (${src})")
endforeach()
forbid_text("${copy_target}" "gapGrindChoice" "the choice flag copied to the persistent target")
# Review C-F3 / B-F2: no group copy of a grind trip for the population (its own R1 request opens the window); a member's
# grind destination this bot needs for an active quest stays (one sorted per-bot list, read once per group choice).
require_text("${policy}" "inline bool GapGroupCopyAllowed(bool gapGrind, bool grindDestination, bool neededForQuest)\n{\n    return !gapGrind || !grindDestination || neededForQuest;\n}" "no group grind copy for the population")
region("${choose}" "bool ChooseGroupTravelTargetAction::Execute(Event& event)" "\n}\n" group_exec)
require_text("${group_exec}" "    bool const gapGrind = ai->IsQuestGapGrind();\n    std::vector<int32> gapNeeded;\n    if (gapGrind)\n        gapNeeded = AI_VALUE(std::vector<int32>, \"needed quest entries\");" "GrindMode 1 first, the quest list once per group choice")
require_text("${group_exec}" "        if (gapGrind && !ai::quest_grind_mode::GapGroupCopyAllowed(true,\n                dynamic_cast<GrindTravelDestination*>(groupTarget->GetDestination()) != nullptr,\n                std::binary_search(gapNeeded.begin(), gapNeeded.end(), groupTarget->GetDestination()->GetEntry())))\n            continue;" "a member's grind trip is no candidate for the population")
require_order("${group_exec}" "if (groupTarget->IsForced())\n            continue;" "GapGroupCopyAllowed(true," "after the forced skip")
require_order("${group_exec}" "GapGroupCopyAllowed(true," "groupTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" "before the member check (no member cooldown from a skipped copy)")
require_order("${group_exec}" "GapGroupCopyAllowed(true," "groupTargets.push_back(groupTarget);" "before the candidate list")
count_text("${choose}" "GapGroupCopyAllowed(" 1 "one group copy rule")
forbid_text("${group_exec}" "SetGapWindowEnd(" "a group copy with a window of its own (the bot's own R1 request opens it)")
forbid_text("${group_exec}" "AI_VALUE2(bool, \"need for quest\"" "a value per creature in the group choice")
count_text("${travel_mgr}" "gapWindowLow = " 1 "the window written only in the snapshot constructor")
count_text("${travel_mgr}" "gapWindowHigh = " 1 "the window written only in the snapshot constructor")
count_text("${travel_mgr}" "ai->IsQuestGapGrind()" 1 "the scope read once, in the constructor (never in the async job)")
region("${travel_mgr}" "bool GrindTravelDestination::IsPossible(const PlayerTravelInfo& info) const\n{" "\n}\n" grind_possible)
require_text("${grind_possible}" "    bool const gapWindow = info.HasGapWindow();\n    if (gapWindow && !ai::quest_grind_mode::InGapWindow(info.GetGapWindowLow(), info.GetGapWindowHigh(), cInfo->MaxLevel))\n        return false;" "R3: template MaxLevel inside the window for the scope")
require_text("${grind_possible}" "    if (!gapWindow && (int32)cInfo->MaxLevel > maxLevel) //@lvl5 max = 3, @lvl60 max = 57\n        return false;" "the old max bound for every other bot")
require_text("${grind_possible}" "    if (!gapWindow && (int32)cInfo->MaxLevel < minLevel) //@lvl5 min = 3, @lvl60 max = 50\n        return false;" "the old min bound for every other bot")
require_text("${grind_possible}" "    int32 maxLevel = std::max(botLevel * (0.5f + levelMod), botLevel - 5.0f + levelBoost);" "old max formula unchanged")
require_text("${grind_possible}" "    int32 minLevel = std::max(botLevel * (0.4f + levelMod), botLevel - 12.0f + levelBoost);" "old min formula unchanged")
require_order("${grind_possible}" "bool const gapWindow = info.HasGapWindow();" "if (cInfo->MinLootGold == 0)" "the money and elite tests stay for the scope")
foreach(banned "IsQuestGapGrind" "GetBotAI" "sRandomPlayerbotMgr" "sPlayerbotAIConfig" "AI_VALUE" "GetGrayLevel")
  forbid_text("${grind_possible}" "${banned}" "the scope computed in IsPossible (async job: only the snapshot value)")
endforeach()
region("${travel_mgr}" "bool TravelMgr::IsLocationLevelValid(const WorldPosition& position, const PlayerTravelInfo& info, uint32 purposeFlag, bool gapWindowApplies)\n{" "\n}\n" level_valid)
# Only where IsPossible runs (GetPartitions onlyPossible): a guild order objective / kill mob fetch keeps the old gate as it
# keeps the old template test; a refresh never runs on a gap grind trip (review B-F5) and its default snapshot has no window.
require_text("${travel_mgr}" "            if (!IsLocationLevelValid(*position, info, purposeFlag, onlyPossible))" "the window only for fetches that ask IsPossible")
count_text("${travel_mgr}" "IsLocationLevelValid(*position, info, purposeFlag" 1 "one GetPartitions call")
require_text("${level_valid}" "    if (info.HasGapWindow() && gapWindowApplies && purposeFlag == (uint32)TravelDestinationPurpose::Grind)\n        return ai::quest_grind_mode::InGapWindow(info.GetGapWindowLow(), info.GetGapWindowHigh(), areaLevel);" "R3: area level in the same window, pure grind points of the scope only")
require_order("${level_valid}" "sPlayerbotAIConfig.questFirstProgressionQuestAreaLevelMargin);\n    }" "if (info.HasGapWindow() && gapWindowApplies && purposeFlag == (uint32)TravelDestinationPurpose::Grind)" "after the gather and quest branches (they stay)")
require_order("${level_valid}" "if (info.HasGapWindow() && gapWindowApplies && purposeFlag == (uint32)TravelDestinationPurpose::Grind)" "if (!areaLevel || (uint32)botLevel < areaLevel)" "replaces the area gate for the scope only")
require_order("${level_valid}" "if (!position.isOverworld() && !canFightElite)\n        areaLevel += 10;" "if (info.HasGapWindow() && gapWindowApplies && purposeFlag" "dungeon +10 still counts")
require_text("${level_valid}" "        int32 grindMinLevel = std::max(rawLevel * (0.4f + levelMod), rawLevel - 12.0f + levelBoost);\n        if ((int32)areaLevel <= grindMinLevel)\n            return false;" "the old zone floor for every other bot")
foreach(banned "IsQuestGapGrind" "GetBotAI" "sRandomPlayerbotMgr" "GetGrayLevel")
  forbid_text("${level_valid}" "${banned}" "the scope computed in IsLocationLevelValid (async job)")
endforeach()
count_text("${travel_mgr}" "info.HasGapWindow()" 2 "the scope bit read in IsPossible and IsLocationLevelValid only")
region("${travel_mgr}" "bool GrindTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const" "\n}\n" grind_active)
foreach(banned "IsQuestGapGrind" "HasGapWindow" "InGapWindow")
  forbid_text("${grind_active}" "${banned}" "IsActive keeps its order (the window reaches it through IsPossible(info))")
endforeach()
region("${travel_mgr}" "DestinationList TravelMgr::GetDestinations(" "\nvoid TravelMgr::GetPartitionsLock" get_destinations)
forbid_text("${get_destinations}" "HasGapWindow" "a window test of its own in GetDestinations")

# ---- 10. Own map only, inside the pinned CandidateSkipOf --------------------------------------------------
region("${choose}" "CandidateSkip CandidateSkipOf(" "\n}\n" skip_fn)
require_text("${skip_fn}" "    if (target->IsGapGrindChoice() && position && !target->IsForced() && !ai::quest_grind_mode::GapGrindStaysOnMap(true,\n            (uint32(destination->GetPurpose()) & uint32(TravelDestinationPurpose::Grind)) != 0, bot->GetMapId(), position->getMapId()))\n        return CandidateSkip::OtherMapGrind;" "own map for the gap grind choice only (review C-F2: flag set only behind GrindMode 1; quest choices and guild orders stay)")
forbid_text("${skip_fn}" "ai->IsQuestGapGrind()" "the own-map rule for every choice of the population (review C-F2)")
require_order("${skip_fn}" "return CandidateSkip::OtherMapGather;" "return CandidateSkip::OtherMapGrind;" "after the gather map skip")
require_order("${skip_fn}" "return CandidateSkip::OtherMapGrind;" "return CandidateSkip::GatherPurposeSuppressed;" "before the gather purpose skip")
require_text("${choose}" "    OtherMapGather,\n    OtherMapGrind,\n    GatherPurposeSuppressed,\n};" "the skip reason")
region("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\nstd::vector<std::string> split(" best)
count_text("${best}" "CandidateSkipOf(ai, bot, target, persistentTarget, excludedTurnInTarget, destination," 2 "still the pre-loop and the main loop only")
require_text("${best}" "                case CandidateSkip::OtherMapGrind:\n                    ai->TellDebug(requester, \"Skipping grind destination on another map (GrindMode 1).\", \"debug travel\");\n                    continue;" "the main loop skips it")
count_text("${choose}" "GapGrindStaysOnMap(" 1 "the own-map rule only in CandidateSkipOf")

# ---- 11. R4: plain grind candidates of the scope inside the window ------------------------------------------
require_text("${grind_target}" "    bool const gapGrind = ai->IsQuestGapGrind();" "one GrindMode 1 test per pass")
count_text("${grind_target}" "ai->IsQuestGapGrind()" 1 "one GrindMode 1 test per pass")
require_text("${grind_target}" "    uint32 const gapLow = gapGrind ? ai::quest_grind_mode::GapWindowLowLevel(bot->GetLevel(), MaNGOS::XP::GetGrayLevel(bot->GetLevel())) : 0;\n    uint32 const gapHigh = gapGrind ? ai::quest_grind_mode::GapWindowHighLevel(bot->GetLevel()) : 0;" "the window once per pass")
region("${grind_target}" "        if (entry && !needForQuest)\n        {" "            else if (urand(0, 100) < 75)" plain_block)
require_text("${plain_block}" "            if (gapGrind && ai::quest_grind_mode::GapTargetSkipped(true, false, unit->GetVictim() == bot, gapLow, gapHigh, unit->GetLevel()))\n            {\n                ++gapSkipped;" "R4 only for the plain source (no quest need, not on the bot)")
require_order("${plain_block}" "if (gapGrind && ai::quest_grind_mode::GapTargetSkipped(" "if (urand(0, 100) < 20 && travelTargetWorking && !isGrindTravelDest)" "R4 before any urand of the candidate")
count_text("${grind_target}" "GapTargetSkipped(" 1 "R4 only inside the plain grind block")
region("${grind_target}" "std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>(\"possible attack targets\")->Get();" "std::list<ObjectGuid> targets = *context->GetValue<std::list<ObjectGuid> >(\"possible targets\");" attacker_loop)
forbid_text("${attacker_loop}" "gap" "the attacker loop gated by R4 (self-defence stays)")
# Recheck (fix 3, deliberate pin change): Calculate calls FindTargetForGrinding again with assistCount 1, 2, ... while the
# target is null, so a count inside FindTargetForGrinding made a pass r4_empty although a later round (a fresh draw of the
# random skips: 20 % travel-working, critter, grey odds) returned a target. Now round 0 hands its R4 skips to the caller
# (the caller's own local, no shared state, no lock) and Calculate counts both once per value pass after its loop:
# r4_skips= round 0's skips (unchanged: first assist round only), r4_empty= with the pass's final target.
region("${grind_target}" "Unit* GrindTargetValue::Calculate()\n{" "\n}\n" grind_calc)
region("${grind_target}" "Unit* GrindTargetValue::FindTargetForGrinding(int assistCount, uint32& round0GapSkipped)\n{" "    return result;\n}\n" grind_find)
require_text("${grind_calc}" "    uint32 round0GapSkipped = 0;\n    while (!target && assistCount < memberCount)\n    {\n        target = FindTargetForGrinding(assistCount++, round0GapSkipped);\n    }" "round 0's skips into a local of the pass (0 when round 0 returns early)")
require_text("${grind_calc}" "    if (round0GapSkipped)\n    {\n        PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::R4Skips), round0GapSkipped);\n        if (ai::quest_grind_mode::GapR4EmptyPass(round0GapSkipped, target != nullptr))\n            PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::R4Empty));\n    }" "r4_skips: round 0's skips in one relaxed add per pass; r4_empty: one more when R4 skipped and the pass's final target is null, in the same block")
require_order("${grind_calc}" "        target = FindTargetForGrinding(assistCount++, round0GapSkipped);\n    }" "    if (round0GapSkipped)\n    {" "r4_skips= / r4_empty= after every assist round (once per value pass, not per round)")
region("${grind_calc}" "    while (!target && assistCount < memberCount)\n    {" "\n    }\n" grind_rounds)
foreach(banned "NoteGrindWindow" "R4Skips" "R4Empty" "GapR4EmptyPass")
  forbid_text("${grind_rounds}" "${banned}" "a [GrindWindow] count per assist round")
  forbid_text("${grind_find}" "${banned}" "r4_skips= / r4_empty= counted inside FindTargetForGrinding (per assist round)")
endforeach()
# Round 0 alone writes its skips, once, after the candidate loop (the attacker early return and the no-candidate return
# leave the caller's 0: not counted, as before).
require_text("${grind_find}" "    if (!assistCount)\n        round0GapSkipped = gapSkipped;\n" "round 0 alone hands over its skips")
require_order("${grind_find}" "    if (result)\n    {\n        logGrind(result, \"selected.\");" "        round0GapSkipped = gapSkipped;" "round 0's skips handed over after the candidate loop")
count_text("${grind_find}" "round0GapSkipped" 2 "the out-parameter written at one site only (signature + the hand-over)")
forbid_text("${attacker_loop}" "round0GapSkipped" "the attacker early return writing the R4 skips")
# No [GrindWindow] count inside the candidate loop (once per pass, not per candidate): r4_skips= / r4_empty= are counted in
# Calculate after its assist loop, from round 0's hand-over and the pass's final target (pinned above).
region("${grind_target}" "    for (std::list<ObjectGuid>::iterator tIter = targets.begin(); tIter != targets.end(); tIter++)" "    if (result)\n    {\n        logGrind(result, \"selected.\");" candidate_loop)
foreach(banned "R4Empty" "GapR4EmptyPass" "NoteGrindWindow" "round0GapSkipped")
  forbid_text("${candidate_loop}" "${banned}" "a [GrindWindow] count inside the candidate loop (per candidate)")
endforeach()
count_text("${grind_target}" "GapR4EmptyPass(" 1 "one r4_empty= rule site")
count_text("${grind_target}" "WindowEvent::R4Empty" 1 "one r4_empty= count site")
foreach(src choose travel_mgr choose_target values ai_cpp mgr)
  forbid_text("${${src}}" "WindowEvent::R4Empty" "r4_empty= counted outside the grind target pass (${src})")
endforeach()
count_text("${grind_target}" "AI_VALUE2(bool, \"need for quest\"" 1 "no new value per creature (the loop's own quest test)")

# ---- 12. R5: the isGrindTravelDest fix only under mode 1 --------------------------------------------------
require_text("${grind_target}" "    bool isGrindTravelDest = travelTarget && typeid(travelTarget->GetDestination()) == typeid(GrindTravelDestination);" "GrindMode 0 / 2 keep the old test")
require_text("${grind_target}" "    if (gapGrind)\n        isGrindTravelDest = travelTarget && dynamic_cast<GrindTravelDestination*>(travelTarget->GetDestination()) != nullptr;" "R5 under mode 1")
count_text("${grind_target}" "dynamic_cast<GrindTravelDestination*>" 1 "R5 once, behind gapGrind")
require_order("${grind_target}" "bool const gapGrind = ai->IsQuestGapGrind();" "isGrindTravelDest = travelTarget && dynamic_cast<GrindTravelDestination*>" "the mode test before the fix")

# ---- 13. [GrindWindow]: counted on the map threads, one line per minute on the world thread, mode 1 alone --------
require_text("${ai_h}" "    static void NoteGrindWindow(uint8 event, uint32 n = 1);" "counter entry")
require_text("${ai_cpp}" "std::atomic<uint32> grindWindowCounts[ai::quest_grind_mode::WindowEventCount] = {};" "relaxed counters")
require_text("${ai_cpp}" "    if (event < ai::quest_grind_mode::WindowEventCount && n)\n        grindWindowCounts[event].fetch_add(n, std::memory_order_relaxed);" "bounded index, relaxed add")
require_text("${ai_cpp}" "        counts[event] = grindWindowCounts[event].exchange(0, std::memory_order_relaxed);\n    return ai::quest_grind_mode::FormatWindowMinuteLine(counts);" "taken with a relaxed exchange")
region("${mgr}" "void ReportGrindWindow()\n{" "\n}\n" report)
require_order("${report}" "    if (!ai::quest_grind_mode::GapFiller(sPlayerbotAIConfig.questFirstProgressionGrindMode))\n        return;" "static uint32 lastReport = 0;" "gated by GrindMode 1 before the clock")
require_text("${report}" "    std::string const line = PlayerbotAI::TakeGrindWindowMinuteLine();\n    if (!line.empty())\n        sLog.outBasic(\"%s\", line.c_str());" "omitted when every count is 0")
foreach(banned "botUpdateTrace" "BotUpdateTrace" "std::mutex")
  forbid_text("${report}" "${banned}" "gated by GrindMode 1 alone, no lock (OP2)")
endforeach()
require_order("${mgr}" "    ReportQuestIdle();" "    ReportGrindWindow();" "once per world update, next to the [QuestIdle] report")
count_text("${mgr}" "ReportGrindWindow();" 1 "one call")
# starts= at the window line, expired= at the window end, no_dest= at an empty fetch, engaged= at the engagement.
require_text("${gap_block}" "                SET_AI_VALUE2(int, \"manual int\", \"gap window logged\", backoffUntil);\n                PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Starts));" "starts= once per window (with its line)")
region("${travel_mgr}" "    if (statusTime != 0 && GetTimeLeft() <= 0 && !IsForced())\n    {" "        return;\n    }" expiry2)
require_text("${expiry2}" "        if (gapWindowEnd && (GetStatus() == TravelStatus::TRAVEL_STATUS_READY || GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL ||\n                GetStatus() == TravelStatus::TRAVEL_STATUS_WORK))\n            PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));" "expired= only for a gap grind trip's window statuses")
require_order("${expiry2}" "PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));" "SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "counted before the expiry")
# Rebase onto P3-b (twow-repo#594, 3391e459): its TRAVEL expiry hook stays directly before SetStatus(EXPIRED) (its own contract),
# the gap expired= block comes before it. The hook counts no failure for a gap trip: OnTravelFailure returns at once for a
# destination without a direct-target tag (TagOf: a direct key and a DirectTargetTag), and a gap trip is a GrindTravelDestination,
# which is no DirectTargetTag - so the window end stays no failure (OB-10 rev. 2 point 3) without a gate of its own.
require_order("${expiry2}" "PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));" "ai::direct_targets::OnTravelFailure(ai, tDestination, \"expired\");" "the gap expired= block before the P3-b expiry hook")
region("${expiry2}" "if (gapWindowEnd && (GetStatus() == TravelStatus::TRAVEL_STATUS_READY" "PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));" gap_expired_block)
forbid_text("${gap_expired_block}" "OnTravelFailure" "a failure hook in the gap expired= block")
region("${direct_targets}" "void ai::direct_targets::OnTravelFailure(PlayerbotAI* ai, TravelDestination* destination, char const* reason)\n{" "\n}\n" p3b_failure)
require_text("${p3b_failure}" "{\n    DirectTargetTag* tag = TagOf(destination);\n    if (!tag)\n        return;" "the P3-b failure hook returns at once for an untagged destination (a gap grind trip)")
require_text("${direct_targets}" "    if (!destination || !IsDirectKey(destination->GetEntry()))\n        return nullptr;\n    return dynamic_cast<DirectTargetTag*>(const_cast<TravelDestination*>(destination));" "a tag only for a direct key and a DirectTargetTag")
require_text("${travel_h}" "class GrindTravelDestination : public EntryTravelDestination\n" "a grind destination is no DirectTargetTag")
region("${choose}" "        // twow-repo#601 S2 / S4 [GrindWindow] no_dest=" "        return false;\n    }" nodest_block)
require_text("${nodest_block}" "        if (ai->IsQuestGapGrind() && futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)))" "no_dest= GrindMode 1 first, grind fetches only")
require_text("${nodest_block}" "            if (ai::quest_grind_mode::GapNoDestDue(uint32(time(nullptr)) < uint32(backoffUntil), backoffUntil,\n                    AI_VALUE2(int, \"manual int\", \"gap window logged\"), AI_VALUE2(int, \"manual int\", \"gap window empty\")))\n            {\n                SET_AI_VALUE2(int, \"manual int\", \"gap window empty\", backoffUntil);\n                PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::NoDest));" "no_dest= once per window inside the backoff")
foreach(banned "\"quest route failures\"" "\"quest route failed at\"" "SET_AI_VALUE2(int, \"manual int\", \"quest route backoff until\"" "NoteTurnInFailure" "NoteQuestIdle")
  forbid_text("${nodest_block}" "${banned}" "an empty window writes no failure (OB-10 rev. 2 point 3)")
endforeach()
# Recheck (fix 3, deliberate pin change): an expired gap trip keeps its window end, so the window end alone counted plain
# engagements after the backoff end (the 30 s grace) as inside a window. engaged= / grey_engaged= also need the quest route
# backoff still running (R1 open), read like R1 (a per-bot value, no value per creature), behind IsQuestGapGrind, before
# the attack. Recheck 3: nor after a silent end on an inactive destination (the trip is EXPIRED, the window end kept).
require_text("${choose_target}" "    Unit* const gapTarget = (ai->IsQuestGapGrind() &&\n        AI_VALUE(TravelTarget*, \"travel target\")->GetGapWindowEnd() > uint32(time(nullptr)) &&\n        AI_VALUE(TravelTarget*, \"travel target\")->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED &&\n        uint32(time(nullptr)) < uint32(std::max(0, AI_VALUE2(int, \"manual int\", \"quest route backoff until\")))) ? GetTarget() : nullptr;" "engaged= GrindMode 1 first, inside a window, gap trip not ended (recheck 3), while the backoff runs (R1 open)")
forbid_text("${choose_target}" "GetGapWindowEnd() > uint32(time(nullptr))) ? GetTarget() : nullptr;" "engaged= on the window end alone (an expired gap trip keeps it)")
count_text("${choose_target}" "\"quest route backoff until\"" 1 "one backoff read in AttackAnythingAction (engaged=)")
require_order("${choose_target}" "AI_VALUE2(int, \"manual int\", \"quest route backoff until\")" "    bool result = AttackAction::Execute(event);" "the backoff read before the attack")
require_text("${choose_target}" "== ai::grind_pick::Source::Grind;" "engaged= the plain grind source only (attackers, victims, quest mobs not counted)")
require_text("${choose_target}" "std::vector<int32> const& needed = AI_VALUE(std::vector<int32>, \"needed quest entries\");" "one per-bot quest list, no value per creature")
require_text("${choose_target}" "                if (gapTarget && gapTarget == grindTarget && gapPlain)\n                {\n                    PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Engaged));\n                    if (gapGrey)\n                        PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::GreyEngaged));" "engaged= / grey_engaged= after the attack started, same target")
count_text("${choose_target}" "AI_VALUE2(bool, \"need for quest\"" 1 "no new value per creature in AttackAnythingAction ([GrindPick] keeps its own)")
count_text("${travel_mgr}" "PlayerbotAI::NoteGrindWindow(" 2 "expired= at the window end (status time) and at a condition end (review C-F1 / B-F1)")
count_text("${choose}" "PlayerbotAI::NoteGrindWindow(" 2 "starts=, no_dest=")
count_text("${grind_target}" "PlayerbotAI::NoteGrindWindow(" 2 "r4_skips=, r4_empty= (OB-10 18.10.)")
count_text("${choose_target}" "PlayerbotAI::NoteGrindWindow(" 2 "engaged=, grey_engaged=")
foreach(src grind_target choose_target)
  forbid_text("${${src}}" "std::mutex" "a lock (${src})")
  forbid_text("${${src}}" "shared_mutex" "a lock (${src})")
endforeach()
# Recheck (fix 3): the mode-1 text says the money grind happens only inside the window (27c04733), never the old "never
# through ... the money grind".
require_text("${conf}" "#       GrindFallbackFailures, GrindFallbackMinutes, DirectTargets.FreshGrindFailures, a travel refresh or a group\n#       member's grind trip (the money grind too only inside that window); the grind trip ends with the backoff" "mode-1 text: the money grind only inside the window")
forbid_text("${conf}" "DirectTargets.FreshGrindFailures, the money grind, a travel" "mode-1 text: the old 'never through ... the money grind' wording")
require_text("${conf}" "[GrindWindow] minute starts= expired= engaged=\n#       grey_engaged= no_dest= r4_skips= r4_empty= once a minute, not when all are 0 (r4_empty= = grind target\n#       passes in which the level window skipped a candidate and no target was left)." "the minute line documented (OB-10 18.10.: r4_empty=)")
foreach(src config_h config_cpp)
  forbid_text("${${src}}" "GrindWindow" "a new config key (${src})")
endforeach()
forbid_text("${conf}" "AiPlayerbot.QuestFirstProgression.GrindWindow" "a new config key")

message(STATUS "GRIND_GAP_WINDOW_SOURCE_CONTRACT=PASS")
