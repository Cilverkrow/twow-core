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

message(STATUS "turnin_stall source contract passed")
