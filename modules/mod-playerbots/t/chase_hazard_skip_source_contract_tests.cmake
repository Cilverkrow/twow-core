# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A14): MovementAction::ChaseTo (the reach actions) ran two navmesh path searches
# (IsValidPosition -> canPathTo, getPathTo) and one LOS ray although with an empty "hazards" list their
# result can only pick one of two detail log lines: IsHazardNearPosition returns false and
# GeneratePathAvoidingHazards returns false at once. With
# AiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards = 1 the hazards value is read once after setZ and,
# when it is empty, those checks are skipped and the chase falls through to the same CHASE_MOTION_TYPE /
# MoveChase path. Default 0 = the original checks verbatim, the hazards value is not read by the gate.
# Helpers are inline (same shape as t/racials_skip_unbuildable_source_contract_tests.cmake and
# t/park_source_contract_tests.cmake in the base tree).
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
    message(FATAL_ERROR "#541 A14: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A14: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A14: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A14: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A14: region start not found: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 tail)
  string(LENGTH "${start_marker}" start_len)
  string(SUBSTRING "${tail}" ${start_len} -1 after)
  string(FIND "${after}" "${end_marker}" end_offset)
  if(end_offset EQUAL -1)
    message(FATAL_ERROR "#541 A14: region end not found after ${start_marker}: ${end_marker}")
  endif()
  math(EXPR len "${start_len} + ${end_offset}")
  string(SUBSTRING "${tail}" 0 ${len} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" config_dist)
read_source("strategy/actions/MovementActions.cpp" move_cpp)

# --- 1. Switch: own member, default 0, read with default false, documented as 0 ---
require_text("${config_h}" "    bool chaseSkipHazardPathWhenNoHazards = false;  // twow-repo#541 (audit A14): "
  "A14 member with default false")
require_order("${config_h}" "bool perfRacialsSkipUnbuildable = false;" "bool chaseSkipHazardPathWhenNoHazards = false;"
  "A14 member after the earlier #541 audit switches")
require_count("${config_h}" "chaseSkipHazardPathWhenNoHazards" 1 "A14 member declared once")
require_text("${config_cpp}"
  "    chaseSkipHazardPathWhenNoHazards = config.GetBoolDefault(\"AiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards\", false);"
  "A14 switch read with default false")
require_count("${config_cpp}" "AiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards" 1 "A14 switch read once")
require_order("${config_cpp}" "perfRacialsSkipUnbuildable = config." "chaseSkipHazardPathWhenNoHazards = config."
  "A14 switch read next to the earlier #541 audit switches")
require_text("${config_dist}" "\nAiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards = 0\n" "A14 conf.dist entry with value 0")
require_count("${config_dist}" "AiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards =" 1 "A14 conf.dist entry once")
forbid_text("${config_dist}" "AiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards = 1" "A14 conf.dist enabled by default")
require_text("${config_dist}" "# twow-repo#541 (audit A14): 1 = while the bot's hazards list is empty"
  "A14 conf.dist documentation")
require_text("${config_dist}" "# Side effects that go away: the mmap tile loads" "A14 conf.dist side-effect disclosure")
require_order("${config_dist}" "\nAiPlayerbot.Perf.RacialsSkipUnbuildable = 0\n"
  "\nAiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards = 0\n" "A14 conf.dist entry next to the #541 performance switches")

# --- Regions ---
region("${move_cpp}" "bool MovementAction::ChaseTo(WorldObject* obj, float distance, float angle)"
  "float MovementAction::MoveDelay(float distance)" chase)
region("${chase}" "const bool chaseHazardChecks"
  "if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)" gated)
region("${move_cpp}" "bool MovementAction::IsHazardNearPosition(" "bool MovementAction::GeneratePathAvoidingHazards(" near)
region("${move_cpp}" "bool MovementAction::GeneratePathAvoidingHazards(" "bool FleeAction::Execute(" gen)
region("${move_cpp}" "bool MovementAction::IsValidPosition(" "bool MovementAction::IsHazardNearPosition(" valid)

# --- 2. Gate: switch first (short-circuit, no read with 0), evaluated after setZ, then the old order ---
require_text("${chase}" "    endPosition.setZ(endPosition.getHeight());\n" "end position height (needed by endPosition.isValid())")
require_text("${chase}"
  "    const bool chaseHazardChecks = !sPlayerbotAIConfig.chaseSkipHazardPathWhenNoHazards ||\n        !AI_VALUE(std::list<HazardPosition>, \"hazards\").empty();\n"
  "A14 gate: switch tested before the hazards read")
require_count("${chase}" "sPlayerbotAIConfig.chaseSkipHazardPathWhenNoHazards" 1 "A14 switch read once in ChaseTo")
require_count("${chase}" "chaseHazardChecks" 3 "A14 gate flag uses (definition, IsHazardNearPosition guard, skip branch)")
require_order("${chase}" "endPosition.setZ(endPosition.getHeight());" "const bool chaseHazardChecks"
  "gate after setZ")
require_order("${chase}" "const bool chaseHazardChecks"
  "    if (chaseHazardChecks && IsHazardNearPosition(endPosition, &hazardPosition))\n"
  "gate before the end-point hazard check")
require_order("${chase}" "if (chaseHazardChecks && IsHazardNearPosition(endPosition, &hazardPosition))"
  "    if (!chaseHazardChecks)\n    {\n"
  "end-point hazard check before the skip branch")
require_order("${chase}" "if (!chaseHazardChecks)" "    else if (IsValidPosition(endPosition, botPosition))\n"
  "skip branch directly before the old IsValidPosition branch")
require_order("${chase}" "else if (IsValidPosition(endPosition, botPosition))" "CHASE_MOTION_TYPE"
  "old IsValidPosition branch before the CHASE_MOTION_TYPE check")
require_order("${chase}" "CHASE_MOTION_TYPE" "if (!endPosition.isValid()) return false;"
  "CHASE_MOTION_TYPE check before the end position validity check")
require_order("${chase}" "if (!endPosition.isValid()) return false;" "mm.MoveChase((Unit*)obj, distance, angle);"
  "end position validity check before MoveChase")
# The skip branch only logs and falls through to the shared CHASE_MOTION_TYPE path.
require_text("${chase}"
  "    if (!chaseHazardChecks)\n    {\n        sLog.outDetail(\"[BOT CHASE] %s -> %s: dist=%.1f no hazards, hazard path checks skipped, using MoveChase\", bot->GetName(), obj->GetName(), distanceToTarget);\n    }\n    else if (IsValidPosition(endPosition, botPosition))\n    {\n"
  "A14 skip branch: one detail log line, then the old branch as else-if")
forbid_text("${chase}" "    if (IsValidPosition(endPosition, botPosition))" "ungated IsValidPosition(endPosition) next to the skip branch")
forbid_text("${chase}" "    if (IsHazardNearPosition(endPosition" "ungated end-point hazard check")

# --- 3. OFF path: the old checks are kept verbatim ---
require_text("${gated}" "        std::vector<WorldPosition> path = botPosition.getPathTo(endPosition,bot);\n        if (GeneratePathAvoidingHazards(path))\n        {\n"
  "OFF path: getPathTo + GeneratePathAvoidingHazards")
require_text("${gated}" "            mm.MovePath(pointsArray, FORCED_MOVEMENT_RUN, false, false);" "OFF path: MovePath")
require_text("${gated}" "            WaitForReach(distance);\n            return true;\n        }\n"
  "OFF path: MovePath branch returns true")
require_text("${gated}" "        sLog.outDetail(\"[BOT CHASE] %s -> %s: dist=%.1f no hazard-avoidance path (no hazards or unroutable), using MoveChase\", bot->GetName(), obj->GetName(), distanceToTarget);\n    }\n    else\n    {\n        sLog.outDetail(\"[BOT CHASE] %s -> %s: dist=%.1f endPos invalid, falling back\", bot->GetName(), obj->GetName(), distanceToTarget);\n    }\n"
  "OFF path: both old log lines and the else branch")
require_text("${gated}" "        if (IsValidPosition(possibleEndPosition, botPosition))\n" "OFF path: perpendicular end point check")
require_count("${gated}" "CalculatePerpendicularPoint(endPoint, hazardPoint, hazardRangeOffset, " 2 "OFF path: left and right end point")

# --- 4. Exactly one hazards read in ChaseTo, no new return in the gated part ---
require_count("${chase}" "\"hazards\"" 1 "hazards value read once in ChaseTo")
require_count("${chase}" "AI_VALUE(std::list<HazardPosition>, \"hazards\")" 1 "hazards AI_VALUE read once in ChaseTo")
forbid_text("${chase}" "GetValue<std::list<HazardPosition>" "second hazards read in another spelling")
forbid_text("${chase}" "GetValue<std::list<HazardPosition> >" "second hazards read in another spelling")
require_count("${gated}" "return " 1 "returns between the gate and CHASE_MOTION_TYPE (only the old MovePath return true)")

# --- 5. No shared state ---
forbid_text("${chase}" "static " "static state in ChaseTo")
forbid_text("${chase}" "mutex" "lock in ChaseTo")
forbid_text("${chase}" "sRandomPlayerbotMgr" "bot manager access in ChaseTo")

# --- 6. Neutrality preconditions: empty hazards -> both helpers return false, IsValidPosition unchanged ---
require_text("${near}" "    if (!hazards.empty())\n" "IsHazardNearPosition loop only for a non-empty list")
require_order("${near}" "if (!hazards.empty())" "return true;" "IsHazardNearPosition true only inside the non-empty block")
require_count("${near}" "return true;" 1 "IsHazardNearPosition returns true at one place")
require_count("${near}" "return " 2 "IsHazardNearPosition returns (true inside the loop, false at the end)")
require_text("${near}" "    return false;\n}\n" "IsHazardNearPosition returns false after the block")
require_text("${gen}" "    std::list<HazardPosition> hazards = AI_VALUE(std::list<HazardPosition>, \"hazards\");\n    if (hazards.empty())\n        return false;\n"
  "GeneratePathAvoidingHazards returns false at once for an empty list")
require_order("${gen}" "if (hazards.empty())\n        return false;" "std::vector<WorldPosition> collidingHazards;"
  "GeneratePathAvoidingHazards empty check before any path work")
require_text("${valid}" "    return botPosition.canPathTo(position, bot) &&\n" "IsValidPosition unchanged (canPathTo)")
require_text("${valid}" "           !IsHazardNearPosition(position);\n" "IsValidPosition unchanged (hazard check)")

message(STATUS "chase_hazard_skip source contract passed")
