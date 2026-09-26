if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#303 part 2: no continental walk to a real-player master, and a far
# walk re-plans instead of running to a stale point.
file(READ "${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" movement)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

string(FIND "${movement}" "bool MovementAction::Follow(Unit* target, float distance, float angle)" begin)
string(SUBSTRING "${movement}" ${begin} -1 tail)
string(FIND "${tail}" "WorldPosition CalculatePerpendicularPoint" end)
string(SUBSTRING "${tail}" 0 ${end} follow)

string(FIND "${follow}" "== far_follow::Decision::HOLD" hold_at)
string(FIND "${follow}" "if (hold)" hold_branch)
string(FIND "${follow}" "if (GetBotAI(player)) //Try to move to where the bot is going" long_move)
string(FIND "${follow}" "bool const moved = MoveTo(target, ai->GetRange(\"follow\"));" walk)
if(hold_at EQUAL -1 OR hold_branch EQUAL -1 OR long_move EQUAL -1 OR walk EQUAL -1 OR
   NOT hold_branch LESS long_move OR NOT hold_branch LESS walk)
  message(FATAL_ERROR "the hold decision must come before any far walk")
endif()
require_text("${follow}" "IsRealPlayer((Player*)target)" "limit only for a real-player target")
require_text("${follow}" "far_follow::ShouldTell(" "rate-limited summon request")
require_text("${follow}" "SetDuration(far_follow::ReplanMs);" "far walk re-plans")
require_text("${config}" "config.GetFloatDefault(\"AiPlayerbot.FarFollowMaxWalkDistance\", 400.0f)" "limit key, default 400")

message(STATUS "FAR_FOLLOW_SOURCE_CONTRACT=PASS")
