cmake_policy(VERSION 3.16)
# twow-repo#601 S2 / S4 (OB-10 rev. 2, 18.10.2026): AiPlayerbot.QuestFirstProgression.GrindMode = 1, the gap filler - the
# GrindMode 2 population grinds only while its quest route backoff runs (R1) and the grind trip ends with that backoff (R2).
# Pins:
#  - the pure policy (GapFiller, the population, R1 GapWindowOpen, R2 GapWindowSeconds / GapStatusMs, the window line),
#  - every new branch asks GrindMode 1 first (IsQuestGapGrind: the config before anything else),
#  - R1 in NeedTravelPurposeValue(Grind): after S1's turn-in yield, before the S0 GrindFallbackMinutes, FreshGrindFailures
#    and 8.32 GrindFallbackFailures lines, an unconditional return (S0 and the gap filler never both), any failure class
#    (OP4), no refresh renewal,
#  - no money grind for the population: the grind request only inside the R1 window,
#  - R2: the gap grind trip's READY / TRAVEL / WORK status time ends with the window; its expiry is no failure (no work
#    timeout, no turn-in / quest loop / move retry / destination death count, no quest route counter),
#  - one [QuestSearch] state=grind_window line per window, state=grind_fallback stays GrindMode 0,
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
count_text("${choose}" "ai->IsQuestGapGrind()" 2 "the gap trip and the request")
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

# ---- 7. No new key, default 0 ------------------------------------------------------------------------
foreach(src config_h config_cpp conf)
  forbid_text("${${src}}" "GapWindow" "a new config key (${src})")
  forbid_text("${${src}}" "GrindGap" "a new config key (${src})")
endforeach()
require_text("${conf}" "\nAiPlayerbot.QuestFirstProgression.GrindMode = 0\n" "GrindMode default 0")
forbid_text("${conf}" "AiPlayerbot.QuestFirstProgression.GrindMode = 1" "GrindMode shipped as 1")

message(STATUS "GRIND_GAP_WINDOW_SOURCE_CONTRACT=PASS")
