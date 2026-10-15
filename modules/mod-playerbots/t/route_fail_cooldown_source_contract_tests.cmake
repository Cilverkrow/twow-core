cmake_policy(VERSION 3.16)
# twow-repo#541 (L1b): behind AiPlayerbot.Perf.RouteFailCooldownSeconds (default 0, clamp 0..300) a
# TravelNodeMap::getRoute(position) search that tried every node pair (and the hearthstone route) without a route
# blocks the same bot and target cell in the existing #416 RouteCooldowns() store, like an exceeded budget. Until then
# the search returns the same empty route at once. Off: no cooldown, no counter - the next search runs as before.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 L1b: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 L1b: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 L1b: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 L1b: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#541 L1b: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("StallGuardPolicy.h" policy)
read_source("TravelNode.cpp" travel_node)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# 1. Switch: default 0 in code and conf, clamped 0..300, documented with the acceptance note.
require_text("${config_h}" "    uint32 perfRouteFailCooldownSeconds = 0;  // twow-repo#541 (L1b)" "member default off")
require_order("${config_h}" "uint32 perfPartialPathReuseMs = 10000;" "uint32 perfRouteFailCooldownSeconds = 0;" "appended after the L1 members")
require_text("${config_cpp}" "perfRouteFailCooldownSeconds = ai::stall_guard::ClampRouteFailCooldownSeconds(config.GetIntDefault(\"AiPlayerbot.Perf.RouteFailCooldownSeconds\", 0));" "load default 0, clamped")
require_text("${config_cpp}" "#include \"playerbot/StallGuardPolicy.h\"" "policy header in the config")
require_text("${policy}" "constexpr std::uint32_t RouteFailCooldownMaxSeconds = 300;" "upper clamp")
require_text("${policy}" "    if (seconds <= 0)\n        return 0;" "0 and below = off")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.RouteFailCooldownSeconds = 0\n" "documented off")
require_text("${conf_dist}" "enable only after OB-30 measurement + owner go - nach Abnahme an):\n# seconds (0..300" "acceptance note")

# 2. getRoute(position): the cooldown is set only on the final no-route return, behind the switch, in the #416 store.
function_body("${travel_node}" "TravelNodeRoute TravelNodeMap::getRoute(WorldPosition startPos, WorldPosition endPos," route_body)
# twow-repo#592 (AiPlayerbot.Perf.RouteFailEarlySkip): the failure cooldown keeps the search's origin (unknown when off).
require_text("${route_body}" "    if (routeBot && sPlayerbotAIConfig.perfRouteFailCooldownSeconds)\n    {\n        ai::stall_guard::RouteCooldowns().Block(routeKey, routeNow + sPlayerbotAIConfig.perfRouteFailCooldownSeconds, routeNow, failOrigin);\n        ai::stall_guard::routeFailBlocks.fetch_add(1, std::memory_order_relaxed);\n    }\n\n    return TravelNodeRoute();" "gated block right before the final no-route return")
string(REGEX MATCHALL "routeNow [+] sPlayerbotAIConfig[.]perfRouteFailCooldownSeconds" fail_blocks "${route_body}")
list(LENGTH fail_blocks fail_block_count)
if(NOT fail_block_count EQUAL 1)
  message(FATAL_ERROR "#541 L1b: expected exactly 1 failure cooldown in getRoute, found ${fail_block_count}")
endif()
# A found route never sets the failure cooldown: the hearthstone route returns before it, the pair loop returns inside.
require_order("${route_body}" "testPathToLoop(startPos, endPos, unit, uid, routePoints, \"route\");\n                    return route;" "routeNow + sPlayerbotAIConfig.perfRouteFailCooldownSeconds" "hearthstone route returns before the cooldown")
# The #416 budget stop keeps its own fixed cooldown.
require_text("${route_body}" "ai::stall_guard::RouteCooldowns().Block(routeKey, routeNow + ai::stall_guard::RouteCooldownSeconds, routeNow);" "budget cooldown unchanged")
# The skip path: the same empty route, the counter only with the switch (and the twow-repo#575 detail flag, only
# when a detail was asked for). With AiPlayerbot.Path.HonestRouteFail the caller turns this empty route into a failed
# MoveTo, so during the cooldown the honest answer costs no search.
require_text("${route_body}" "    if (routeBot && ai::stall_guard::RouteCooldowns().IsBlocked(routeKey, routeNow))\n    {\n        if (sPlayerbotAIConfig.perfRouteFailCooldownSeconds)   // twow-repo#541 (L1b): [RouteFail] skips\n            ai::stall_guard::routeCooldownSkips.fetch_add(1, std::memory_order_relaxed);\n        if (outDetail)   // twow-repo#575 (PATH-01 #1)\n            outDetail->cooldownSkip = true;\n        return TravelNodeRoute();\n    }" "cooldown skip unchanged apart from the gated counter and the detail flag")
require_order("${route_body}" "RouteCooldowns().IsBlocked(routeKey, routeNow)" "uint32 const routeStart = WorldTimer::getMSTime();" "skip before any search work")

# 3. [RouteFail]: gated minute line, world thread, plain call before the PerfMon Init.
function_body("${rnd_mgr}" "void ReportRouteFail()" report_body)
require_text("${report_body}" "if (!sPlayerbotAIConfig.perfRouteFailCooldownSeconds || now < lastReport + 60)\n        return;" "nothing when off, once a minute")
require_text("${report_body}" "ai::stall_guard::routeFailBlocks.exchange(0, std::memory_order_relaxed);" "blocks taken")
require_text("${report_body}" "ai::stall_guard::routeCooldownSkips.exchange(0, std::memory_order_relaxed);" "skips taken")
require_text("${report_body}" "sLog.outBasic(\"[RouteFail] blocks=%llu skips=%llu cooldown_seconds=%u\"" "minute line")
function_body("${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd_body)
require_order("${upd_body}" "\n    ReportPathReuse();" "\n    ReportRouteFail();" "after [PathReuse]")
require_order("${upd_body}" "\n    ReportRouteFail();" "sPerformanceMonitor.Init(0, 0);" "report before the PerfMon Init")
forbid_text("${upd_body}" "routeFailBlocks" "counter handling inside UpdateAIInternal")

message(STATUS "route_fail_cooldown source contract passed")
