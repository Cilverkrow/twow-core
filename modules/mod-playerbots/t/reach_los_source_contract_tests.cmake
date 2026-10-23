# twow-repo#594 (AiPlayerbot.Rotation.ReachLosClose, Linalella 23.10.2026): a hostile reach target out of sight is chased to
# half the distance (ReachLosPolicy.h) instead of 75 % of the range. Switch default 0, friendly rule unchanged, the hotfix 8.2
# progress check stays the stop condition (a target that never comes into sight still ends as unreachable), ConfigEcho.

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
    message(FATAL_ERROR "#594 ReachLosClose: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#594 ReachLosClose: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#594 ReachLosClose: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("ReachLosPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/ReachTargetActions.h" reach_h)

# Switch, default 0, documented, echoed.
require_text("${config_h}" "bool rotationReachLosClose = false;" "default off")
require_text("${config_cpp}" "rotationReachLosClose = config.GetBoolDefault(\"AiPlayerbot.Rotation.ReachLosClose\", false);" "config read")
require_text("${config_cpp}" "{\"Rotation.ReachLosClose\", Value(rotationReachLosClose)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.ReachLosClose = 0\n" "documented, 0 by default")
forbid_text("${conf_dist}" "AiPlayerbot.Rotation.ReachLosClose = 1" "default 1 in the dist")

# Pure rules: in sight 75 %, friendly 0.9 unchanged, hostile 0.5 only with the switch, capped at preferred.
forbid_text("${policy}" "#include \"" "game headers in the pure policy")
require_text("${policy}" "constexpr float FriendFactor = 0.9f;" "friendly factor unchanged")
require_text("${policy}" "constexpr float HostileFactor = 0.5f;" "hostile factor")
require_text("${policy}" "    if (inLos)\n        return preferredDist;\n    if (isFriend)\n        return std::min(distanceToTarget * FriendFactor, preferredDist);\n    if (closeHostile)\n        return std::min(distanceToTarget * HostileFactor, preferredDist);\n    return preferredDist;" "the whole rule")

# Call site: the policy with the switch, in place of the old inline rule; the 8.2 progress check stays before it.
require_text("${reach_h}" "#include \"playerbot/ReachLosPolicy.h\"" "policy include")
require_text("${reach_h}" "chaseDist = ai::reach_los::ChaseDistance(inLos, isFriend, sPlayerbotAIConfig.rotationReachLosClose, distanceToTarget,\n                        preferredDist);" "the policy at the call site")
forbid_text("${reach_h}" "(isFriend ? std::min(distanceToTarget * 0.9f, preferredDist) : preferredDist)" "the old inline rule")
require_text("${reach_h}" "if (!isFriend && ai->CheckReachProgress(target, distanceToTarget))" "the 8.2 progress check (stop condition)")
require_order("${reach_h}" "ai->CheckReachProgress(target, distanceToTarget)" "ai::reach_los::ChaseDistance(" "progress check before the chase distance")
# isUseful still forces a move without line of sight.
require_text("${reach_h}" "                        // Force move if not in los\n                        if (bot->IsWithinLOSInMap(target, true))" "isUseful forces a move without LOS")

message(STATUS "reach_los source contract passed")
