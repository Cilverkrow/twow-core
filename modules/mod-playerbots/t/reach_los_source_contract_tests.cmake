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
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)

# Switch: a mode 0 / 1 / 2 (CLI-ROT: in-realm A/B), default 0, unknown -> 0, documented, echoed.
require_text("${config_h}" "uint32 rotationReachLosClose = 0;" "default off, a mode")
require_text("${config_cpp}" "rotationReachLosClose = config.GetIntDefault(\"AiPlayerbot.Rotation.ReachLosClose\", 0);" "config read")
require_text("${config_cpp}" "if (rotationReachLosClose > 2)\n        rotationReachLosClose = 0;" "an unknown mode is off")
require_text("${config_cpp}" "{\"Rotation.ReachLosClose\", Value(rotationReachLosClose)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.ReachLosClose = 0\n" "documented, 0 by default")
forbid_text("${conf_dist}" "AiPlayerbot.Rotation.ReachLosClose = 1" "default 1 in the dist")
forbid_text("${conf_dist}" "AiPlayerbot.Rotation.ReachLosClose = 2" "default 2 in the dist")

# Pure rules: only the CohortPolicy include; in sight 75 %, friendly 0.9 unchanged, hostile 0.5 only with the mode, the floor.
require_text("${policy}" "#include \"CohortPolicy.h\"" "the shared parity rule")
forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_text("${policy}" "constexpr float FriendFactor = 0.9f;" "friendly factor unchanged")
require_text("${policy}" "constexpr float HostileFactor = 0.5f;" "hostile factor")
require_text("${policy}" "    if (inLos)\n        return { preferredDist, Kind::Unchanged };\n    if (isFriend)\n        return { std::min(distanceToTarget * FriendFactor, preferredDist), Kind::Unchanged };\n    if (!closeHostile)\n        return { preferredDist, Kind::Unchanged };" "old rule unless hostile + mode")
require_text("${policy}" "    float const floor = std::min(preferredDist, floorDist);\n    float const closer = distanceToTarget * HostileFactor;\n    if (closer < floor)\n        return { floor, Kind::Floored };\n    return { std::min(closer, preferredDist), Kind::Closed };" "half the distance, never inside the floor")
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "mode 1 all, 2 cohort B")
require_text("${policy}" "return spellMinRange > 0.0f ? spellMinRange : tooCloseDistance;" "floor = spell min range, else tooCloseDistance")

# Call site: mode + cohort, the floor, the counters per cohort; the 8.2 progress check stays before it.
require_text("${reach_h}" "#include \"playerbot/ReachLosPolicy.h\"" "policy include")
require_text("${reach_h}" "bool const closeHostile = losMode && ai::reach_los::Applies(losMode, bot->GetGUIDLow());" "the mode and the cohort at the call site")
require_text("${reach_h}" "float const floorDist = ai::reach_los::Floor(minSpellRange, sPlayerbotAIConfig.tooCloseDistance);" "the floor")
require_text("${reach_h}" "if (ai->GetSpellRange(spellName, &maxSpellRange, &minSpellRangeOut))" "the spell's minimum range is read")
require_text("${reach_h}" "chaseDist = std::max(chaseDist, std::min(preferredDist, floorDist));" "the floor after the contact distance")
require_text("${reach_h}" "else if (ai::cohort::InB(bot->GetGUIDLow()))\n                            ai::reach_los::closedB.fetch_add(1, std::memory_order_relaxed);\n                        else\n                            ai::reach_los::closedA.fetch_add(1, std::memory_order_relaxed);" "counted per cohort (leak check)")
forbid_text("${reach_h}" "(isFriend ? std::min(distanceToTarget * 0.9f, preferredDist) : preferredDist)" "the old inline rule")
require_text("${reach_h}" "if (!isFriend && ai->CheckReachProgress(target, distanceToTarget))" "the 8.2 progress check (stop condition)")
require_order("${reach_h}" "ai->CheckReachProgress(target, distanceToTarget)" "ai::reach_los::ChaseDistance(" "progress check before the chase distance")
require_text("${reach_h}" "                        // Force move if not in los\n                        if (bot->IsWithinLOSInMap(target, true))" "isUseful forces a move without LOS")

# Minute line: gated by the mode alone, called from the world update.
require_text("${mgr_cpp}" "if (!sPlayerbotAIConfig.rotationReachLosClose || now < lastReport + 60)\n        return;" "minute line gated by the mode")
require_text("${mgr_cpp}" "    ReportReachLos();" "minute line called")

message(STATUS "reach_los source contract passed")
