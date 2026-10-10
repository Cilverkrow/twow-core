# twow-repo#541 (audit A02, card 52): behind AiPlayerbot.FindCorpseLazySpot (default 0 = off)
# FindCorpseAction::Execute evaluates the branch test of its moving part
# (!AllowActivity(DETAILED_MOVE_ACTIVITY) && !HasPlayerNearby(moveToPos)) before the safe revive
# spot, once, with the identical expression. The two branches that never read moveToPos (waiting out
# the teleport delay, already moving) then skip the FleeManager / random-point search. Switch off:
# skipSafeSpot stays false and the branch test is evaluated where it always was.
# The HasPlayerNearby(moveToPos) overload quirk (binds to HasPlayerNearby(float) via
# WorldPosition::operator bool) is deliberately kept.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A02: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A02: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A02: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text regex expected description)
  string(REGEX MATCHALL "${regex}" hits "${text}")
  list(LENGTH hits n)
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#541 A02: ${description}: expected ${expected}, found ${n}")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A02: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("strategy/actions/ReviveFromCorpseAction.cpp" revive)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# Switch: default off everywhere, documented.
require_text("${config_h}" "bool findCorpseLazySpot = false;" "member default off")
require_text("${config_cpp}" "findCorpseLazySpot = config.GetBoolDefault(\"AiPlayerbot.FindCorpseLazySpot\", false);" "config default 0")
require_text("${conf_dist}" "\nAiPlayerbot.FindCorpseLazySpot = 0\n" "documented key, value 0")

# Helpers mirror the original expressions exactly.
function_body("${revive}" "static uint32 FindCorpseTeleportDelay(Player* bot, Corpse* corpse)" delay_body)
require_text("${delay_body}" "uint32 delay = sServerFacade.GetDistance2d(bot, corpse) / bot->GetSpeed(MOVE_RUN);" "same delay expression")
require_text("${delay_body}" "return std::min(delay, uint32(10 * MINUTE));" "same 10 min cap")
function_body("${revive}" "static bool FindCorpseBotIsMoving(Player* bot)" moving_body)
require_text("${moving_body}" "#ifndef MANGOSBOT_ZERO\n    return bot->IsMovingIgnoreFlying();\n#else\n    return bot->IsMoving();\n#endif" "same moving test per expansion")

function_body("${revive}" "bool FindCorpseAction::Execute(Event& event)" exec)

# The hoist: gated by the switch, only on the FleeManager path, identical expression, evaluated once.
set(gate "if (sPlayerbotAIConfig.findCorpseLazySpot && corpseDist < sPlayerbotAIConfig.reactDistance && !moveToMaster)")
set(hoist "lazyNonDetailed = !ai->AllowActivity(DETAILED_MOVE_ACTIVITY) && !ai->HasPlayerNearby(moveToPos);")
set(skip "skipSafeSpot = lazyNonDetailed ? !(deadTime > FindCorpseTeleportDelay(bot, corpse)) : FindCorpseBotIsMoving(bot);")
require_text("${exec}" "bool skipSafeSpot = false;" "skip defaults to the old path")
require_text("${exec}" "${gate}" "switch gate on the FleeManager path only")
require_text("${exec}" "${hoist}" "identical hoisted branch test")
require_text("${exec}" "${skip}" "skip only in the two branches that never read moveToPos")
require_order("${exec}" "bool moveToMaster = " "${gate}" "hoist after moveToMaster is known")
require_order("${exec}" "within reclaimDist & no mobs near" "${gate}" "hoist after the revive-now exits")
require_order("${exec}" "${gate}" "${hoist}" "hoist inside the gate")
require_order("${exec}" "${hoist}" "${skip}" "skip decided from the hoisted value")
require_order("${exec}" "${skip}" "moveToPos = masterPos;" "skip decided before the spot block")
require_order("${exec}" "moveToPos = masterPos;" "else if (!skipSafeSpot)" "master path untouched")
require_order("${exec}" "else if (!skipSafeSpot)" "FleeManager manager(bot, reclaimDist, 0.0, urand(0, 1), moveToPos);" "FleeManager behind the skip")

# Switch off must leave all three locals at their old-path values (review of A02): the initial
# values are pinned, the whole gated block is pinned verbatim (so no assignment can slip outside
# its braces), and each local is assigned only in its declaration and once inside the gate.
# Otherwise e.g. a stray lazyNonDetailedKnown = true; would force nonDetailed = false with the
# switch off and send every ghost to MoveTo.
require_text("${exec}" "bool lazyNonDetailedKnown = false;" "known flag defaults to the old path")
require_text("${exec}" "bool lazyNonDetailed = false;" "hoisted value default")
require_text("${exec}"
  "    bool lazyNonDetailedKnown = false;\n    bool lazyNonDetailed = false;\n    bool skipSafeSpot = false;\n    ${gate}\n    {\n        ${hoist}\n        lazyNonDetailedKnown = true;\n        ${skip}\n    }\n\n    //If we are getting close"
  "gated block verbatim, assignments only inside the gate")
require_count("${exec}" "lazyNonDetailedKnown[ \t]*=[ \t]*true" 1 "known flag set once (inside the gate)")
require_count("${exec}" "lazyNonDetailedKnown[ \t]*[|&^]?=[^=]" 2 "known flag assignments (declaration + gate)")
require_count("${exec}" "lazyNonDetailed[ \t]*[|&^]?=[^=]" 2 "hoisted value assignments (declaration + gate)")
require_count("${exec}" "skipSafeSpot[ \t]*[|&^]?=[^=]" 2 "skip assignments (declaration + gate)")
require_order("${exec}" "${gate}" "lazyNonDetailedKnown = true;" "known flag set inside the gate")
require_order("${exec}" "lazyNonDetailedKnown = true;" "//If we are getting close" "known flag set before the spot block")

# Branch test: hoisted value when known, otherwise the original expression at the original place.
require_text("${exec}" "bool const nonDetailed = lazyNonDetailedKnown\n        ? lazyNonDetailed\n        : (!ai->AllowActivity(DETAILED_MOVE_ACTIVITY) && !ai->HasPlayerNearby(moveToPos));\n    if (nonDetailed)" "fallback to the original branch test")
require_order("${exec}" "GetReachableRandomPointOnGround(bot, reclaimDist, urand(0, 1))" "bool const nonDetailed = lazyNonDetailedKnown" "branch test after the spot block")
require_order("${exec}" "if (nonDetailed)" "uint32 delay = sServerFacade.GetDistance2d(bot, corpse) / bot->GetSpeed(MOVE_RUN);" "original delay kept in the branch")

# The helpers are copies: the in-branch originals they mirror must not drift either (review of
# A02). Otherwise the skip could fire in the teleport or MoveTo case, which reads the spot.
set(branch_delay "uint32 delay = sServerFacade.GetDistance2d(bot, corpse) / bot->GetSpeed(MOVE_RUN); //Time a bot would take to travel to it's corpse.")
set(branch_cap "delay = std::min(delay, uint32(10 * MINUTE)); //Cap time to get to corpse at 10 minutes.")
set(branch_moving "#ifndef MANGOSBOT_ZERO\n        if (bot->IsMovingIgnoreFlying())\n            moved = true;\n#else\n        if (bot->IsMoving())\n            moved = true;\n#endif\n        if (moved)")
require_text("${exec}" "${branch_delay}" "original in-branch delay expression")
require_text("${exec}" "${branch_cap}" "original in-branch 10 min cap")
require_text("${exec}" "${branch_moving}" "original in-branch moving test per expansion")
require_order("${exec}" "if (nonDetailed)" "${branch_cap}" "in-branch cap inside the non-detailed branch")
require_order("${exec}" "${branch_delay}" "${branch_cap}" "in-branch delay before its cap")
require_order("${exec}" "${branch_cap}" "if (deadTime > delay)" "teleport test after the cap")
require_order("${exec}" "if (deadTime > delay)" "${branch_moving}" "moving test in the else branch")
require_count("${exec}" "if \\(deadTime > delay\\)" 1 "single teleport test")
set(a02_code "${delay_body}\n${moving_body}\n${exec}")
require_count("${a02_code}" "uint32\\(10 \\* MINUTE\\)" 2 "10 min cap (helper + branch)")
require_count("${a02_code}" "IsMovingIgnoreFlying\\(\\)" 2 "IsMovingIgnoreFlying (helper + branch)")
require_count("${a02_code}" "->IsMoving\\(\\)" 2 "IsMoving (helper + branch)")
require_count("${a02_code}" "GetDistance2d\\(bot, corpse\\) / bot->GetSpeed\\(MOVE_RUN\\)" 2 "delay expression (helper + branch)")
require_count("${exec}" "ai->AllowActivity\\(DETAILED_MOVE_ACTIVITY\\)" 2 "AllowActivity(DETAILED_MOVE_ACTIVITY) occurrences (hoist + fallback)")
require_count("${exec}" "ai->HasPlayerNearby\\(" 2 "HasPlayerNearby occurrences (hoist + fallback)")
require_count("${exec}" "FleeManager manager\\(" 1 "single FleeManager")
require_text("${exec}" "sLog.outDetail(\"[BOT CORPSE] %s: find corpse - no detailed-move activity, waiting out teleport delay" "wait branch unchanged")
require_text("${exec}" "sLog.outDetail(\"[BOT CORPSE] %s: find corpse - already moving towards corpse\", bot->GetName());" "moving branch unchanged")

# Out of scope: the HasPlayerNearby overload is not "fixed"; no new shared state.
forbid_text("${exec}" "HasPlayerNearby(moveToPos," "overload fix (out of scope, changes behaviour)")
forbid_text("${exec}" "HasPlayerNearby(corpsePos" "superset player check (not the identical expression)")
forbid_text("${revive}" "thread_local" "per-thread cache")
forbid_text("${revive}" "shared_mutex" "shared_mutex")
forbid_text("${exec}" "static " "static state in Execute")

message(STATUS "find_corpse_lazy_spot source contract passed")
