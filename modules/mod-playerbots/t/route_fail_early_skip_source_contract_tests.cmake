cmake_policy(VERSION 3.16)
# twow-repo#592 (PATH-02 phase 2, group A no. 6): behind AiPlayerbot.Perf.RouteFailEarlySkip (default 0, clamp 0..2,
# only with AiPlayerbot.Perf.RouteFailCooldownSeconds > 0) a getFullPath search that repeats a stored route failure of
# the same bot is left out before its direct attempt (level 2) or only counted (level 1, shadow, early_wrong = its direct
# attempt then succeeded). The failure cooldown keeps start, exact target and direct gap in the bounded #416 store (one
# short std::mutex); a match needs start and target each within 1 yd and a stored gap of at least spell distance + 5 yd
# (a stored gap below 0 never matches; a deferred direct attempt that never ran stores no origin). An early skip is no
# [RouteTime] build (the lookup comes before RouteTimeScope). Off: no lookup, no origin, the [RouteFail] line unchanged. Pure rules: route_fail_early_skip_policy_tests.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#592 EarlySkip: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#592 EarlySkip: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#592 EarlySkip: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#592 EarlySkip: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#592 EarlySkip: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("StallGuardPolicy.h" policy)
read_source("TravelNode.cpp" node_cpp)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("PathFailPolicy.h" fail_policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# 1. Switch: default 0, clamped 0..2, documented with the acceptance note.
require_text("${config_h}" "    uint32 perfRouteFailEarlySkip = 0;  // twow-repo#592 (PATH-02)" "member default off")
require_text("${config_cpp}" "perfRouteFailEarlySkip = ai::stall_guard::ClampRouteFailEarlySkip(config.GetIntDefault(\"AiPlayerbot.Perf.RouteFailEarlySkip\", 0));" "load default 0, clamped")
require_text("${policy}" "constexpr std::uint32_t RouteFailEarlySkipMax = 2;" "upper clamp")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.RouteFailEarlySkip = 0\n" "documented off")
require_text("${conf_dist}" "enable only after OB-30 measurement + owner go -\n# nach Abnahme an)" "acceptance note")

# 2. The rule: start and exact target within 1 yd (3D), stored gap far from the goal or no path, known origin only.
require_text("${policy}" "constexpr float EarlySkipYards = 1.0f;" "1 yd")
require_text("${policy}" "constexpr float EarlySkipGapMargin = 5.0f;" "gap margin")
require_text("${policy}" "    if (!stored.known || stored.startMap != now.startMap)\n        return false;" "unknown origin / other start map never")
require_text("${policy}" "    if (dsx * dsx + dsy * dsy + dsz * dsz > reach || dtx * dtx + dty * dty + dtz * dtz > reach)\n        return false;" "start and target within reach")
require_text("${policy}" "    return stored.directGap >= 0.0f && stored.directGap >= minDirectGap;" "stored gap measured and far from the goal")

# 3. The store: bounded as before, one short std::mutex, origin only for the failure cooldown.
require_text("${policy}" "    static constexpr std::size_t MaxEntries = 4096;" "4096 entries")
require_text("${policy}" "it = it->second.until <= nowSeconds ? entries.erase(it) : std::next(it);" "expired entries dropped on overflow")
require_text("${policy}" "            if (entries.size() >= MaxEntries)\n                entries.clear();" "hard bound")
require_text("${policy}" "RouteFailOrigin const& origin = RouteFailOrigin())" "origin unknown by default (budget block)")
forbid_text("${policy}" "shared_mutex" "a shared mutex in the store")
function_body("${node_cpp}" "TravelNodeRoute TravelNodeMap::getRoute(WorldPosition startPos, WorldPosition endPos," route)
require_text("${route}" "    if (sPlayerbotAIConfig.perfRouteFailEarlySkip && !startPath.empty())\n        failOrigin = ai::stall_guard::MakeRouteFailOrigin(" "origin only with the switch and a direct path")
require_order("${route}" "failOrigin = ai::stall_guard::MakeRouteFailOrigin(" "std::vector<TravelNode*> startNodes = getNodes(" "origin taken before the search changes the start path")
require_text("${route}" "    if (precheckOn && precheck->directDeferred && failOrigin.known)\n    {\n        if (precheck->directLate)\n            failOrigin.directGap = precheck->lateGap;\n        else\n            failOrigin.known = false;\n    }" "deferred direct attempt: late gap, or no origin when it never ran")
require_order("${route}" "RunRoutePrecheck(*precheck," "failOrigin.known = false;" "origin dropped after the pre-check decided")
require_order("${route}" "failOrigin.known = false;" "RouteCooldowns().Block(routeKey, routeNow + sPlayerbotAIConfig.perfRouteFailCooldownSeconds, routeNow, failOrigin);" "origin dropped before the cooldown is set")
require_text("${route}" "RouteCooldowns().Block(routeKey, routeNow + sPlayerbotAIConfig.perfRouteFailCooldownSeconds, routeNow, failOrigin);" "failure cooldown keeps the origin")
require_text("${route}" "ai::stall_guard::RouteCooldowns().Block(routeKey, routeNow + ai::stall_guard::RouteCooldownSeconds, routeNow);" "budget cooldown without origin")

# 4. getFullPath: the lookup before the direct attempt, only with both switches; level 2 returns the empty route.
function_body("${node_cpp}" "TravelPath TravelNodeMap::getFullPath(WorldPosition startPos, WorldPosition endPos, Unit* unit, ai::path_fail::RouteFailDetail* outDetail)" full)
require_text("${full}" "    if (sPlayerbotAIConfig.perfRouteFailEarlySkip && sPlayerbotAIConfig.perfRouteFailCooldownSeconds)" "both switches")
require_text("${full}" "if (earlyBot && !earlyBot->GetTransport() && ai::stall_guard::RouteCooldowns().EarlySkip(" "player bot without transport")
require_text("${full}" "sPlayerbotAIConfig.spellDistance + ai::stall_guard::EarlySkipGapMargin, storedGap))" "gap limit from the spell distance")
require_text("${full}" "            if (sPlayerbotAIConfig.perfRouteFailEarlySkip >= 2)\n            {\n                ai::stall_guard::routeCooldownSkips.fetch_add(1, std::memory_order_relaxed);" "only level 2 skips, counted as a skip")
require_order("${full}" "ai::stall_guard::RouteCooldowns().EarlySkip(" "beginPath = endPos.getPathFromPath({ startPos }, unit, 40);" "lookup before the direct attempt")
require_order("${full}" "ai::stall_guard::RouteCooldowns().EarlySkip(" "std::shared_lock<std::shared_timed_mutex> nodeMapLock(sTravelNodeMap.m_nMapMtx);" "lookup before the node map lock")
require_order("${full}" "                    outDetail->directGap = storedGap;\n                }\n                return movePath;" "RouteTimeScope routeTimeScope(" "an early skip is no [RouteTime] build")
require_text("${full}" "        if (earlySkipMatch)   // twow-repo#592: a direct path the early skip would have lost (shadow)\n            ai::stall_guard::routeEarlyWrong.fetch_add(1, std::memory_order_relaxed);" "early_wrong on a direct success")
require_order("${full}" "if (earlySkipMatch)" "        return TravelPath(beginPath);" "early_wrong counted before the direct answer")
require_text("${fail_policy}" "    bool earlySkip = false;" "detail flag")
require_text("${move_cpp}" "(d.earlySkip ? \" early_skip=1\" : \"\")" "detail shows an early skip")

# 5. [RouteFail]: the old line unchanged when off, the two counters appended only with the switch.
function_body("${rnd_mgr}" "void ReportRouteFail()" report)
require_text("${report}" "    if (!sPlayerbotAIConfig.perfRouteFailEarlySkip)\n        sLog.outBasic(\"[RouteFail] blocks=%llu skips=%llu cooldown_seconds=%u\",\n            (unsigned long long)blocks, (unsigned long long)skips, sPlayerbotAIConfig.perfRouteFailCooldownSeconds);" "old line when off")
require_text("${report}" "\"[RouteFail] blocks=%llu skips=%llu cooldown_seconds=%u early_skips=%llu early_wrong=%llu\"" "extended line when on")
require_text("${report}" "ai::stall_guard::routeEarlySkips.exchange(0, std::memory_order_relaxed)" "early_skips taken")
require_text("${report}" "ai::stall_guard::routeEarlyWrong.exchange(0, std::memory_order_relaxed)" "early_wrong taken")

message(STATUS "route_fail_early_skip source contract passed")
