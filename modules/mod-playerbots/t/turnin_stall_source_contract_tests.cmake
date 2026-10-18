# twow-repo#587 Z4 (owner 14.10.2026 via OB-00): AiPlayerbot.ZoneExit.TurnInStallCounts (default 0). A stage-1 recovery of a
# finished quest's turn-in counts as a failed turn-in, so the #485 park takes an unreachable taker out of the loop.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#587 Z4 turn-in stall: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#587 Z4 turn-in stall: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#587 Z4 turn-in stall: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#587 Z4 turn-in stall: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)

require_text("${config_h}" "bool zoneExitTurnInStallCounts = false;" "switch default off")
require_text("${config_cpp}" "zoneExitTurnInStallCounts = config.GetBoolDefault(\"AiPlayerbot.ZoneExit.TurnInStallCounts\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.ZoneExit.TurnInStallCounts = 0" "documented")

between("${travel}" "case turnin_recovery::RecoveryAction::RecomputeRoute:" "case turnin_recovery::RecoveryAction::SuppressRouteAndCooldown:" stage1)
require_order("${stage1}" "TraceQuestCommit(tDestination, \"blocked\", \"stall_recompute_route\");" "if (IsProgressAwareTurnIn())" "counted after the trace")
require_text("${stage1}" "bool const parked = sPlayerbotAIConfig.zoneExitTurnInStallCounts && NoteTurnInFailure(questId, \"stall_recompute\");" "turn-ins only, the park behind the switch")
require_order("${stage1}" "unreachable_target::LongWindow().Add(key, questId, bot->GetGUIDLow());" "SetStatus(TravelStatus::TRAVEL_STATUS_READY);" "the recovery itself unchanged")
# The park machinery itself (threshold, window, finished-quest check) stays the #485 one.
between("${travel}" "bool TravelTarget::NoteTurnInFailure(uint32 questId, char const* reason)" "\n}\n" note)
require_text("${note}" "if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId))" "finished, not handed in")

# Owner 14.10.: nothing hidden - the [UnreachableTarget] line and the count run whatever the switch says.
require_order("${stage1}" "if (IsProgressAwareTurnIn())" "bool const parked = sPlayerbotAIConfig.zoneExitTurnInStallCounts && NoteTurnInFailure(questId, \"stall_recompute\");" "logging outside the switch")
require_order("${stage1}" "bool const parked = sPlayerbotAIConfig.zoneExitTurnInStallCounts" "[UnreachableTarget] bot=%u level=%u quest=%u entry=%d kind=turn_in" "line after the decision")
require_text("${stage1}" "unreachable_target::LongWindow().Add(key, questId, bot->GetGUIDLow());" "counted for the hourly top")
read_source("RandomPlayerbotMgr.cpp" mgr)
require_text("${mgr}" "[UnreachableTop] window_min=60 total=%u targets=%u top=%s" "hourly top line")
string(FIND "${mgr}" "if (sPlayerbotAIConfig.zoneExitTurnInStallCounts)" top_gated)
if(NOT top_gated EQUAL -1)
  message(FATAL_ERROR "#587 Z4: the unreachable top must not depend on the switch")
endif()

# ---------------------------------------------------------------------------------------------------------------------
# twow-repo#601 S3c (OB-10 rev. 2, 18.10.2026): AiPlayerbot.ZoneExit.TurnInTravelStall (default 0) + TurnInNearRadius.
# Near the taker only getting closer is progress; a stall there parks the turn-in structurally and the bot leaves the
# target; the second work timeout of the same turn-in parks it too; a rescue reset never lifts a park.
# ---------------------------------------------------------------------------------------------------------------------
function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 S3c turn-in travel stall: forbidden ${description}: ${needle}")
  endif()
endfunction()

read_source("ProgressAwareTurnInRecoveryPolicy.h" recovery)
read_source("TravelMgr.h" travel_h)

# Keys: the switch off in code and dist; the radius only acts with it.
require_text("${config_h}" "bool zoneExitTurnInTravelStall = false;" "S3c switch default off")
require_text("${config_h}" "float zoneExitTurnInNearRadius = 60.0f;" "S3c near radius")
require_text("${config_cpp}" "zoneExitTurnInTravelStall = config.GetBoolDefault(\"AiPlayerbot.ZoneExit.TurnInTravelStall\", false);" "S3c config default 0")
require_text("${config_cpp}" "zoneExitTurnInNearRadius = config.GetFloatDefault(\"AiPlayerbot.ZoneExit.TurnInNearRadius\", 60.0f);" "S3c radius read")
require_text("${conf_dist}" "AiPlayerbot.ZoneExit.TurnInTravelStall = 0" "S3c switch documented off")
require_text("${conf_dist}" "AiPlayerbot.ZoneExit.TurnInNearRadius = 60" "S3c radius documented")
forbid_text("${conf_dist}" "AiPlayerbot.ZoneExit.TurnInTravelStall = 1" "S3c switch shipped on")

# The pure near-zone rule: 3 yd closer than the best so far, best set with the target; radius 0 = old rule.
require_text("${recovery}" "constexpr float NearProgressDistance = 3.0f;" "3 yd near progress")
require_text("${recovery}" "return nearRadius > 0.0f && sameMap && distance <= nearRadius;" "near zone needs the radius and the taker's map")
require_text("${recovery}" "float nearRadius = 0.0f;" "near zone off by default in the observation")
require_text("${recovery}" "bool const nearZone = InNearZone(current.distance, current.mapId == current.targetMapId, current.nearRadius);" "near zone from the observation")
require_text("${recovery}" "bool const progressed = nearZone ? mapChanged || current.distance + NearProgressDistance <= state.bestDistance :" "near zone: only a new best distance")
require_order("${recovery}" "state.bestDistance = current.distance;\n        state.lastProgressAt = current.now;" "if (current.paused)" "best distance set with a new target")
# The radius reaches the observation only with the switch and only for a finished quest's turn-in (not objectives).
between("${travel}" "turnin_recovery::RecoveryAction TravelTarget::ObserveTurnInProgress()" "\n}\n" observe)
require_text("${observe}" "if (sPlayerbotAIConfig.zoneExitTurnInTravelStall && IsProgressAwareTurnIn())\n        observation.nearRadius = sPlayerbotAIConfig.zoneExitTurnInNearRadius;" "radius behind the switch, turn-ins only")
require_order("${observe}" "observation.nearRadius = " "return turnin_recovery::Observe(turnInRecovery, observation," "radius set before the observation")

# Stage 1 inside the near zone, switch tested first: the near-stall handler, then return (no READY on that path).
require_order("${stage1}" "if (IsProgressAwareTurnIn())" "if (sPlayerbotAIConfig.zoneExitTurnInTravelStall && wPosition &&" "near stall for turn-ins only")
require_text("${stage1}" "if (sPlayerbotAIConfig.zoneExitTurnInTravelStall && wPosition &&\n                        turnin_recovery::InNearZone(Distance(bot), bot->GetMapId() == wPosition->getMapId(), sPlayerbotAIConfig.zoneExitTurnInNearRadius))" "switch first, then the near zone")
require_order("${stage1}" "OnNearTurnInStall(questId);\n                        return;" "bool const parked = sPlayerbotAIConfig.zoneExitTurnInStallCounts && NoteTurnInFailure(questId, \"stall_recompute\");" "near stall leaves before the Z4 count")
# Structural park, not abandonable (a finished quest is never dropped for it), the Z4 line with reason=near_stall,
# counted for [UnreachableTop], then the target is left.
require_text("${travel_h}" "void OnNearTurnInStall(uint32 questId);" "near stall handler declared")
between("${travel}" "void TravelTarget::OnNearTurnInStall(uint32 questId)" "\n}\n" near_stall)
require_text("${near_stall}" "bool const parked = ParkTurnInNow(questId, \"structural_unreachable_near\", false, abandonDue);" "structural park, never abandonable")
require_order("${near_stall}" "ParkTurnInNow(questId, \"structural_unreachable_near\"" "[UnreachableTarget] bot=%u level=%u quest=%u entry=%d kind=turn_in map=%u zone=%u dest_map=%u dest_x=%.1f dest_y=%.1f dest_z=%.1f reason=near_stall parked=%u switch=%u" "near_stall line after the park, Z4 format")
require_text("${near_stall}" "unreachable_target::LongWindow().Add(key, questId, bot->GetGUIDLow());" "near stall counted for the hourly top")
require_order("${near_stall}" "unreachable_target::LongWindow().Add(key, questId, bot->GetGUIDLow());" "SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "parked: the target is left")
require_text("${near_stall}" "SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" "not parked: the route cooldown")
forbid_text("${near_stall}" "TRAVEL_STATUS_READY" "READY -> TRAVEL loop on the same taker")
forbid_text("${near_stall}" "DropQuest(" "abandon of a finished quest")
forbid_text("${near_stall}" "ParkStructuralTurnIn(" "S1 abandon helper (count pin 3 stays in ChooseTravelTargetAction.cpp)")

# Work timeouts: the second one of the same turn-in parks it (switch first), the #485 count stays after it.
require_text("${travel_h}" "turnin_recovery::WorkTimeoutRing workTimeoutTurnIns;" "per-bot work timeout ring")
require_text("${recovery}" "constexpr std::uint32_t WorkTimeoutRingSize = 8;" "ring of 8 quests")
between("${travel}" "void TravelTarget::OnWorkTimeout()" "\n}\n" work_timeout)
require_order("${work_timeout}" "if (IsProgressAwareTurnIn())" "if (sPlayerbotAIConfig.zoneExitTurnInTravelStall)" "turn-ins only")
require_order("${work_timeout}" "if (sPlayerbotAIConfig.zoneExitTurnInTravelStall)" "turnin_recovery::RecordWorkTimeout(workTimeoutTurnIns, questId)" "switch before the ring")
require_order("${work_timeout}" "ParkTurnInNow(questId, \"structural_work_timeout\", false, abandonDue);" "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"work_timeout\");" "park before the kept #485 count")

# A rescue reset (S1 ResetOnProgressEvents) resets the search, never the park book: the bot is not sent back 3.5 km.
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)
between("${ai_cpp}" "void PlayerbotAI::ApplyQuestRouteReset(uint32 events)" "\n}\n" route_reset)
forbid_text("${route_reset}" "travel target" "travel target touched by the S1 progress reset")
forbid_text("${route_reset}" "TravelTarget" "travel target touched by the S1 progress reset")
between("${ai_cpp}" "void PlayerbotAI::Reset(bool full)" "\n}\n" ai_reset)
forbid_text("${ai_reset}" "RESET_AI_VALUE(TravelTarget*" "travel target (park book) recreated by the AI reset")
forbid_text("${ai_cpp}" "turnInParks" "park book outside TravelTarget")
forbid_text("${ai_h}" "turnInParks" "park book outside TravelTarget")
forbid_text("${mgr}" "turnInParks" "park book cleared by the rescue")
forbid_text("${travel}" "turnInParks = " "park book replaced")
forbid_text("${travel}" "turnInParks.entries.fill(" "park book cleared")
forbid_text("${travel}" "turnInParks.entries = " "park book cleared")

message(STATUS "turnin_stall source contract passed")
