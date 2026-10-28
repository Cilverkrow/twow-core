# twow-repo#601 cand4-B (AiPlayerbot.RosterFarMove.ArriveSafe, OB-00 28.10.2026): 382 of 1022 deaths in 3 h came within 60 s of a
# far-move arrival; the arrival point lies ArriveBackYards before a kill objective, usually inside its camp. With the switch
# the arrival is checked right before the teleport (the Arrive step, not the Start up to 240 s before): a point where a
# creature would attack the bot at once (the core's proximity aggro without the LOS test) is given up for one 30 yd further
# back, at most 4 times; none free = the farthest valid one; a point further back lets the bot walk the rest (far-move
# decline for that target). Off: the same arrival and the same [FarMove] line as before. The rules are tested in
# t/far_move_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#601 FarMove ArriveSafe: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 FarMove ArriveSafe: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle out_var)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" found)
  if(NOT found EQUAL ${expected})
    message(FATAL_ERROR "#601 FarMove ArriveSafe: ${description} (found ${found}, expected ${expected}): ${needle}")
  endif()
endfunction()

read_source("FarMovePolicy.h" policy)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" dist)

# The rules: aggro radius + margin within the z range (fliers keep theirs), 4 x 30 yd, first free else farthest valid.
require_text("${policy}" "    return attackDistance > 0.0f && distance <= attackDistance + ArriveSafeMarginYards && (canFly || dz <= ArriveSafeZYards);" "the threat rule")
require_text("${policy}" "constexpr float ArriveSafeStepYards = 30.0f;\nconstexpr std::uint32_t ArriveSafeSteps = 4;" "4 steps of 30 yd")
require_text("${policy}" "        if (!candidates[i].threats)\n        {\n            choice.outcome = i == 0 ? ArriveOutcome::Kept : ArriveOutcome::Shifted;" "the first free point")
require_text("${policy}" "        if (!anyValid || candidates[i].threats <= fewest)\n        {\n            fewest = candidates[i].threats;\n            choice.index = i;\n        }" "none free: the fewest threats, the farthest of equal ones (pre-review M2)")
require_text("${policy}" "    choice.outcome = anyValid ? ArriveOutcome::Unsafe : ArriveOutcome::None;" "unsafe or none")
require_text("${policy}" "    return !until || now < until;" "the decline lapses (pre-review M3)")

# The threat check mirrors the core's MoveInLineOfSight conditions (no LOS), with the creature's own aggro radius to the bot.
require_text("${move_cpp}" "        if (!creature->IsAlive() || creature->IsNeutralToAll() || creature->GetVictim() || !creature->CanInitiateAttack() ||\n            !creature->IsHostileTo(bot))\n            return false;" "the core's aggro preconditions")
require_text("${move_cpp}" "        return far_move::Threatens(std::sqrt(dx * dx + dy * dy), creature->GetAttackDistance(bot), dz, creature->CanFly());" "the core's aggro radius")
require_text("${move_cpp}" "        float const dz = far_move::AggroDz(creature->GetPositionZ() - point.getZ(),\n            creature->GetObjectBoundingRadius() + bot->GetObjectBoundingRadius());" "the core's z distance (GetDistanceZ minus both radii)")
# Only loaded grids are searched (dont_load stays true): an unloaded arrival grid is not loaded from the bot, it counts as unchecked;
# so does a point in another continent region (its camp creatures live in another Map, updated by another thread).
require_text("${move_cpp}" "    if (!map || point.getMapId() != bot->GetMapId() || !map->IsLoaded(point.getX(), point.getY()))\n        return -1;\n    if (sMapMgr.GetContinentInstanceId(point.getMapId(), point.getX(), point.getY()) != map->GetInstanceId())\n        return -1;" "no check in an unloaded grid or another region (pre-review M1)")
require_text("${move_cpp}" "    Cell::VisitGridObjects(point.getX(), point.getY(), map, searcher, far_move::ArriveSafeSearchYards);" "the grid search without loading")
# Every further point goes through FarMoveArrivalPoint (navmesh snap, danger map); the steps stop at the route start.
require_text("${move_cpp}" "            float const back = baseBack + far_move::ArriveSafeStepYards * float(count);\n            if (back >= routeLength)\n                break;\n            WorldPosition point;\n            if (FarMoveArrivalPoint(bot, from, to, back, point))" "the steps through the navmesh / danger-map check, not past the start")
# The ArriveSafe decline lapses; the hotfix 8.36 decline keeps no time limit; the decline check reads it.
require_text("${move_cpp}" "                    lastMove.farMoveDeclinedUntil = now + far_move::ArriveSafeDeclineSeconds;" "the ArriveSafe decline lapses")
require_text("${move_cpp}" "                        lastMove.farMoveDeclinedUntil = 0;   // twow-repo#601 cand4-B: the 8.36 decline holds until the target changes" "the 8.36 decline unchanged")
require_text("${move_cpp}" "            far_move::DeclineActive(uint64(lastMove.farMoveDeclinedUntil), uint64(time(nullptr)));" "the decline check")
require_text("${move_cpp}" "    walkRest = choice.index > 0;\n    return points[choice.index];" "a point further back walks the rest")

# The wiring at the Arrive step: the switch per bot (cohort), before the teleport, the decline after the clear.
# The check rewrites the stored arrival point before the unchanged hotfix 8.36 line reads it (cost_833 pins that line).
set(arrive_call "                if (lastMove.farMoveArriveSet && ai::cohort::Applies(sPlayerbotAIConfig.rosterFarMoveArriveSafe, bot->GetGUIDLow()))\n                    lastMove.farMoveArrive = SafeFarMoveArrival(bot, farStart, endPos, lastMove.farMoveArrive, walkRest);\n                WorldPosition const arrive = lastMove.farMoveArriveSet ? lastMove.farMoveArrive : endPos;")
require_text("${move_cpp}" "${arrive_call}" "the check at Arrive")
require_count("${move_cpp}" "SafeFarMoveArrival(bot, " 1 "one check, at Arrive only (not at Start)")
require_order("${move_cpp}" "${arrive_call}" "                return bot->TeleportTo(arrive.getMapId(), arrive.getX(), arrive.getY(), arrive.getZ(), farStart.getAngleTo(endPos));" "the check before the teleport")
require_order("${move_cpp}" "                lastMove.clear();\n                lastMove.farMoveAt = 0;\n                if (walkRest)" "                    lastMove.farMoveDeclinedUntil = now + far_move::ArriveSafeDeclineSeconds;\n                }\n                counters.arrived.fetch_add(1" "the decline after the clear")

# The minute line: the arrival counters and the cost only with the switch; off, the old line.
require_text("${move_cpp}" "                    if (sPlayerbotAIConfig.rosterFarMoveArriveSafe)\n                    {" "the line part only with the switch")
require_text("${move_cpp}" "\" safe_checked=%u safe_kept=%u safe_shifted=%u safe_unsafe=%u safe_unchecked=%u check_us_avg=%u check_us_max=%u\"" "the arrival counters and the cost")
require_text("${move_cpp}" "                    sLog.outBasic(\"[FarMove] started=%u retargeted=%u arrived=%u watched=%u arrive_alt=%u arrive_walk=%u arrive_deaths=%u%s\"," "the old fields first")

# The switch: modes 0/1/2, unknown = off, echoed, documented with default 0.
require_text("${config_h}" "    uint32 rosterFarMoveArriveSafe = 0;" "the member, default 0")
require_text("${config_cpp}" "    rosterFarMoveArriveSafe = config.GetIntDefault(\"AiPlayerbot.RosterFarMove.ArriveSafe\", 0);\n    if (rosterFarMoveArriveSafe > 2)\n        rosterFarMoveArriveSafe = 0;" "the parse")
require_text("${config_cpp}" "            {\"RosterFarMove.ArriveSafe\", Value(rosterFarMoveArriveSafe)}," "the ConfigEcho entry")
require_text("${dist}" "AiPlayerbot.RosterFarMove.ArriveSafe = 0" "the dist default")
