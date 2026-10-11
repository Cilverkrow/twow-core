cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet D67 (AiPlayerbot.Diet.WanderSoloGuard, default 0): "wander near" of a bot without a
# follow target fires only on a FOLLOW motion, the only state in which its "stop follow" is useful. With 0, or with a
# follow target, the old check runs. "wander far" / "wander medium" stay untouched (stale free-move center snapshot).
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

file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/RangeTriggers.h" range_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/generic/WanderStrategy.cpp" wander_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/FollowActions.cpp" follow_actions)

# Switch: default 0 in member, loader and dist config.
require_text("${config_h}" "bool dietWanderSoloGuard = false;" "dietWanderSoloGuard member default false")
require_text("${config_cpp}" "dietWanderSoloGuard = config.GetBoolDefault(\"AiPlayerbot.Diet.WanderSoloGuard\", false);" "WanderSoloGuard read with default 0")
require_text("${config_dist}" "\nAiPlayerbot.Diet.WanderSoloGuard = 0\n" "WanderSoloGuard = 0 in the dist config")

# wander near: guard first, only for a missing follow target, answers with the FOLLOW motion check; old path after it.
region("${range_triggers_h}" "class WanderNearTrigger : public StopFollowTrigger" "\n    };" wander_near)
set(guard "if (sPlayerbotAIConfig.dietWanderSoloGuard && !AI_VALUE(Unit*, \"follow target\"))\n                return bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE;")
require_text("${wander_near}" "${guard}" "D67 wander near guard behind the switch")
require_order("${wander_near}" "${guard}" "if (StopFollowTrigger::IsActive())\n                return true;" "D67 guard before the old check")
require_text("${wander_near}" "return AI_VALUE2(bool, \"can free move\", \"wandermin\");" "D67 old wandermin check kept")

# wander far / medium untouched (their free-move center is a cached position snapshot and can fire today).
region("${range_triggers_h}" "class WanderFarTrigger : public Trigger" "\n    };" wander_far)
region("${range_triggers_h}" "class WanderMediumTrigger : public Trigger" "\n    };" wander_medium)
forbid_text("${wander_far}" "dietWanderSoloGuard" "D67 must not gate wander far")
forbid_text("${wander_medium}" "dietWanderSoloGuard" "D67 must not gate wander medium")
require_text("${wander_far}" "return !AI_VALUE2(bool, \"can free move\", \"wandermax\");" "wander far unchanged")

# StopFollowTrigger itself unchanged (also registered as "stop follow").
region("${range_triggers_h}" "class StopFollowTrigger : public Trigger" "\n    };" stop_follow)
forbid_text("${stop_follow}" "dietWanderSoloGuard" "D67 must not change the stop follow trigger")

# Neutrality anchors: wander near only pushes "stop follow", whose isUseful starts with the FOLLOW motion check.
require_text("${wander_strategy}" "\"wander near\",\n        NextAction::array(0, new NextAction(\"stop follow\", ACTION_IDLE), NULL)" "wander near node pushes only stop follow")
region("${follow_actions}" "bool StopFollowAction::isUseful()" "\n}\n" stop_follow_useful)
require_text("${stop_follow_useful}" "if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)\n        return false;" "StopFollowAction::isUseful starts with the FOLLOW motion check")

message(STATUS "diet D67 wander solo guard source contract passed")
