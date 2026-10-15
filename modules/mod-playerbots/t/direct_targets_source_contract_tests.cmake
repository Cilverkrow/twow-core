# twow-repo#594 / P3 (owner 15.10.2026, direct travel targets; AiPlayerbot.DirectTargets.*, all default off):
# P3-0 splits [QuestFirstRoute] not_active by reason; P3-a gives fresh quest-first bots (level <= FreshMaxLevel) a short
# quest route backoff and their own grind threshold. With the switches off the 8.1 backoff, the 8.32 grind fallback, Z1b
# and the #485 park are unchanged (agreed with OB-10 15.10.).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" values)
file(READ "${PB_SOURCE_DIR}/QuestSearchPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)

# Switches, all off by default.
require_text("${config_cpp}" "directTargetsEnabled = config.GetBoolDefault(\"AiPlayerbot.DirectTargets.Enable\", false);" "master default off")
require_text("${config_cpp}" "directTargetsTraceInactive = config.GetBoolDefault(\"AiPlayerbot.DirectTargets.TraceInactive\", false);" "trace default off")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.DirectTargets.FreshBackoffSeconds\", 0)" "fresh backoff default 0")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.DirectTargets.FreshGrindFailures\", 0)" "fresh grind default 0")
foreach(line "AiPlayerbot.DirectTargets.Enable = 0" "AiPlayerbot.DirectTargets.TraceInactive = 0"
             "AiPlayerbot.DirectTargets.FreshBackoffSeconds = 0" "AiPlayerbot.DirectTargets.FreshGrindFailures = 0")
  require_text("${config_dist}" "\n${line}\n" "dist ${line}")
endforeach()

# Policy: one backoff formula; not fresh = the 8.1 rule (2, 4, 8, 10 min) untouched.
region("${policy}" "inline uint32_t RouteBackoffSeconds(uint32_t failures, bool fresh = false, uint32_t freshSeconds = 0)" "\n}" backoff)
require_order("${backoff}" "if (fresh && freshSeconds)\n        return freshSeconds;" "uint32_t seconds = RouteBackoffBaseSeconds;" "fresh case before the 8.1 rule")
require_text("${policy}" "constexpr uint32_t RouteBackoffBaseSeconds = 120;" "8.1 base 2 min")
require_text("${policy}" "constexpr uint32_t RouteBackoffMaxSeconds = 600;" "8.1 max 10 min")
region("${policy}" "inline bool FreshBot(bool directTargets, uint32_t level, uint32_t maxLevel)" "\n}" fresh)
require_text("${fresh}" "return directTargets && maxLevel && level <= maxLevel;" "fresh only with the master switch")

# The rejection branch, in OB-10's order: trace -> widen -> Z1b -> 8.1 backoff -> #485 park -> P3 counts at the end.
region("${choose}" "if (futureTravelPurpose == \"quest\")\n        {\n            // #421 A: which filters took every candidate." "        return false;\n    }" branch)
require_order("${branch}" "TraceQuestRouteRejection(bot," "WidenQuestSearch(uint32(destinationList.size()));" "trace before widen")
require_order("${branch}" "WidenQuestSearch(uint32(destinationList.size()));" "ai->RequestQuestRescue(uint32(time(nullptr)));" "widen before Z1b")
require_order("${branch}" "rescueRequested = true;" "SET_AI_VALUE2(int, \"manual int\", \"quest route failures\", int(failures));" "Z1b before the backoff")
require_order("${branch}" "SET_AI_VALUE2(int, \"manual int\", \"quest route failures\", int(failures));" "if (NoteNoRouteTurnIns(context, travelTarget," "failure count still grows, then the park")
require_order("${branch}" "if (NoteNoRouteTurnIns(context, travelTarget," "ai::quest_search::DirectTargetStats();" "P3 counts at the end of the branch")
require_text("${branch}" "bool const shortBackoff = freshBot && !rescueRequested && sPlayerbotAIConfig.directTargetsFreshBackoffSeconds;" "a Z1b rescue wins")
require_text("${branch}" "ai::quest_search::RouteBackoffSeconds(failures, true, sPlayerbotAIConfig.directTargetsFreshBackoffSeconds) :
                    ai::quest_search::RouteBackoffSeconds(failures);" "the single backoff formula, 8.1 call unchanged")
# Z1b sees every rejection exactly as before (sum untouched, notActive is the total).
require_text("${branch}" "uint32 const otherRejects = lastRejects.hubFilter + lastRejects.zoneLevel + lastRejects.dangerMap + lastRejects.notActive +
                    lastRejects.enemyZone + lastRejects.knownInactive + lastRejects.deathSuppressed + lastRejects.turnInSuppressed;" "Z1b sum unchanged")
region("${branch}" "if (sPlayerbotAIConfig.zoneExitStrandedRescueAfter && UsesQuestFirstProgression(bot))" "ai::quest_search::DirectTargetStats();" z1b_to_park)
forbid_text("${z1b_to_park}" "notActiveReason[" "reason split inside the Z1b / backoff / park sums")

# P3-0: notActive counted unconditionally, the reason only with the trace switch, after it.
require_text("${choose}" "std::to_string((uint32)round(destination->DistanceTo(bot))) + \"y\", \"debug travel\");
                ++lastRejects.notActive;
" "notActive counted unconditionally, right after the debug line")
require_order("${choose}" "++lastRejects.notActive;" "if (sPlayerbotAIConfig.directTargetsTraceInactive)\n                    ++lastRejects.notActiveReason[InactiveReasonOf(ai, destination, PlayerTravelInfo(bot))];" "total first, split only with the switch")
region("${choose}" "ai::quest_search::RejectCounts::InactiveReason InactiveReasonOf(" "\n}\n" reason_fn)
require_order("${reason_fn}" "relation->IsPossible(info)" "can accept quest npc::" "IsActive order: possible before the mark")
require_order("${reason_fn}" "can accept quest npc::" "IsHostileTo(bot)" "IsActive order: mark before hostile")

# 8.32 grind fallback: the fresh threshold only for a fresh bot with the value set, before the unchanged 8.32 line.
require_order("${values}" "if (ai::quest_search::FreshBot(sPlayerbotAIConfig.directTargetsEnabled, bot->GetLevel(), sPlayerbotAIConfig.directTargetsFreshMaxLevel) &&
                sPlayerbotAIConfig.directTargetsFreshGrindFailures)
                return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.directTargetsFreshGrindFailures) &&" "return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures) &&" "fresh branch before the unchanged 8.32 line")

# Minute line before the PerfMon Init (no return in UpdateAIInternal).
region("${mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "sPerformanceMonitor.Init(0, 0);" upd)
require_order("${upd}" "\n    ReportRouteFail();" "\n    ReportDirectTargets();" "[DirectTarget] after [RouteFail]")
region("${mgr}" "void ReportDirectTargets()" "\n}" report)
require_text("${report}" "if ((!sPlayerbotAIConfig.directTargetsEnabled && !sPlayerbotAIConfig.directTargetsTraceInactive) || now < lastReport + 60)" "nothing written with both switches off")
foreach(field enabled fresh_rejections fresh_backoffs rescue_skips fresh_grind fresh_max_level backoff_seconds grind_failures)
  require_text("${report}" "${field}=" "[DirectTarget] field ${field}")
endforeach()

message(STATUS "DIRECT_TARGETS_SOURCE_CONTRACT=PASS")
