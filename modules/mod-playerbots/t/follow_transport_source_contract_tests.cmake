if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#303: a transport GUID without a server-side transport must not
# freeze follow, and the far-follow fallback is observable before its fix.
file(READ "${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" movement)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

function(function_region text signature next_signature output)
  string(FIND "${text}" "${signature}" begin)
  if(begin EQUAL -1)
    message(FATAL_ERROR "Missing function ${signature}")
  endif()
  string(SUBSTRING "${text}" ${begin} -1 tail)
  string(FIND "${tail}" "${next_signature}" end)
  if(end EQUAL -1)
    set(${output} "${tail}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${tail}" 0 ${end} region)
    set(${output} "${region}" PARENT_SCOPE)
  endif()
endfunction()

function_region("${movement}" "bool MovementAction::FollowOnTransport" "void MovementAction::WaitForReach(float distance)" transport)
string(FIND "${transport}" "bot->m_movementInfo.ClearTransportData();" clear_stale)
string(FIND "${transport}" "(bot->GetTransport() || target->GetTransport())" real_transport)
string(FIND "${transport}" "ai->StopMoving();" stop)
if(clear_stale EQUAL -1 OR real_transport EQUAL -1 OR stop EQUAL -1 OR NOT clear_stale LESS real_transport OR NOT real_transport LESS stop)
  message(FATAL_ERROR "FollowOnTransport must clear a stale bot t_guid and require a real transport before stopping the bot")
endif()
require_text("${transport}" "if (!bot->GetTransport() && !bot->m_movementInfo.t_guid.IsEmpty())" "stale t_guid only when the bot has no transport")

# Diagnostic gate: far follow and transport decisions leave a snapshot.
function_region("${movement}" "bool MovementAction::Follow(Unit* target, float distance, float angle)" "WorldPosition CalculatePerpendicularPoint" follow)
require_text("${follow}" "LogFollowDiag(ai, bot, target, hold ? \"far_follow_hold\" : \"far_follow\")" "far-follow snapshot")
require_text("${transport}" "LogFollowDiag(ai, bot, target, \"transport_switch\")" "transport snapshot")
require_text("${movement}" "if (!sPlayerbotAIConfig.followDiagnostics" "diagnostics behind the switch")
require_text("${config}" "config.GetBoolDefault(\"AiPlayerbot.FollowDiagnostics\", false)" "diagnostics off by default")

message(STATUS "FOLLOW_TRANSPORT_SOURCE_CONTRACT=PASS")
