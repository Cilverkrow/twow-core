cmake_policy(VERSION 3.16)
# twow-repo#592 (PATH-02 phase 2, group A no. 2): behind AiPlayerbot.Perf.RouteTimeTrace (default 0) every node route
# build (TravelNodeMap::getFullPath) is timed by phase, one [RouteTime] line a minute, the M:route engine trace token
# gets the build's times after pts= and [RouteFailDetail] gets them at the end; dispatch_* times the MovePoint path
# search in DispatchMovement (only with generatePath). Off: no clock read, no counter, no token text, no line.
# Pure policy (classes, counters, line format): route_time_policy_tests.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#592 RouteTime: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#592 RouteTime: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#592 RouteTime: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#592 RouteTime: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#592 RouteTime: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR at "${at} + ${needle_len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("RouteTimePolicy.h" policy)
read_source("PathFailPolicy.h" fail_policy)
read_source("TravelNode.cpp" node_cpp)
read_source("WorldPosition.cpp" pos_cpp)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# 1. Switch: default off in code and conf.
require_text("${config_h}" "    bool perfRouteTimeTrace = false;  // twow-repo#592 (PATH-02)" "member default off")
require_text("${config_cpp}" "perfRouteTimeTrace = config.GetBoolDefault(\"AiPlayerbot.Perf.RouteTimeTrace\", false);" "load default off")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.RouteTimeTrace = 0\n" "documented off")

# 2. Policy header: relaxed atomics only, no lock, no shared mutex; the detail carries the build's times.
forbid_text("${policy}" "mutex" "a lock in the route time counters")
forbid_text("${policy}" "memory_order_seq_cst" "non-relaxed counters")
require_text("${policy}" "static thread_local RouteTimes* active = nullptr;" "per-thread active build")
require_text("${fail_policy}" "ai::route_time::RouteTimes times;" "times in the route-fail detail")

# 3. getFullPath: one scope with the switch; every clock read behind the build's times.
function_body("${node_cpp}" "TravelPath TravelNodeMap::getFullPath(WorldPosition startPos, WorldPosition endPos, Unit* unit, ai::path_fail::RouteFailDetail* outDetail)" full)
require_text("${full}" "RouteTimeScope routeTimeScope(sPlayerbotAIConfig.perfRouteTimeTrace, outDetail);" "scope with the switch")
require_text("${full}" "ai::route_time::RouteTimes* const routeTimes = routeTimeScope.Times();" "times null when off")
require_order("${full}" "RouteTimeScope routeTimeScope(" "beginPath = endPos.getPathFromPath({ startPos }, unit, 40);" "scope before the direct attempt")
require_order("${full}" "std::uint64_t const directStartUs = routeTimes ? ai::bot_update::NowUs() : 0;" "beginPath = endPos.getPathFromPath({ startPos }, unit, 40);" "direct clock before the direct attempt")
require_order("${full}" "std::uint64_t const lockStartUs = routeTimes ? ai::bot_update::NowUs() : 0;" "std::shared_lock<std::shared_timed_mutex> nodeMapLock(sTravelNodeMap.m_nMapMtx);" "lock wait timed")
require_order("${full}" "std::uint64_t const buildStartUs = routeTimes ? ai::bot_update::NowUs() : 0;" "movePath = route.buildPath(beginPath, endPath);" "buildPath timed")
require_text("${full}" "            routeTimes->directOk = routeTimes->found = true;\n        return TravelPath(beginPath);" "direct success marked")
require_text("${full}" "    if (routeTimes)\n        routeTimes->found = !movePath.empty();\n    return movePath;" "route result marked before the last return")
count_text("${full}" "NowUs()" full_clocks)
count_text("${full}" "routeTimes ? ai::bot_update::NowUs() : 0" full_gated)
if(NOT full_clocks EQUAL full_gated)
  message(FATAL_ERROR "#592 RouteTime: getFullPath reads the clock outside 'routeTimes ? NowUs() : 0' (${full_clocks} vs ${full_gated})")
endif()
# The scope: active only with the switch, restored on every return, counted once per build.
function_body("${node_cpp}" "    RouteTimeScope(bool on, ai::path_fail::RouteFailDetail* outDetail) : detail(outDetail)" scope_ctor)
require_order("${scope_ctor}" "        if (!on)\n            return;" "ai::route_time::Active() = &times;" "nothing set when off")
require_text("${node_cpp}" "        if (!times.on)\n            return;\n        times.totalUs = ai::bot_update::SinceUs(startUs);" "nothing counted when off")
require_text("${node_cpp}" "        ai::route_time::Active() = previous;\n        ai::route_time::Global().AddBuild(times);" "active build restored, one count per build")
# The scope's AddBuild is the only one, and TravelNode.cpp still starts with its header comment (a stray copy of the
# AddBuild line once landed at file scope in line 1: no contract saw it, only the compiler).
count_text("${node_cpp}" "AddBuild(" node_add_builds)
if(NOT node_add_builds EQUAL 1)
  message(FATAL_ERROR "#592 RouteTime: expected exactly one AddBuild in TravelNode.cpp (RouteTimeScope), found ${node_add_builds}")
endif()
string(FIND "${node_cpp}" "// botpch.h supplied the boost headers; the module build has no precompiled\n" node_header_at)
if(NOT node_header_at EQUAL 0)
  message(FATAL_ERROR "#592 RouteTime: TravelNode.cpp must start with its header comment '// botpch.h supplied ...'")
endif()

# 4. getRoute(position): the phases read the active build; every clock read behind it.
function_body("${node_cpp}" "TravelNodeRoute TravelNodeMap::getRoute(WorldPosition startPos, WorldPosition endPos," route)
require_text("${route}" "ai::route_time::RouteTimes* const routeTimes = ai::route_time::Active();" "active build read once")
require_order("${route}" "std::uint64_t const nodesStartUs" "std::vector<TravelNode*> startNodes = getNodes(startPos, -1, transportEntry), endNodes = getNodes(endPos);" "getNodes timed")
require_order("${route}" "std::uint64_t const astarStartUs" "TravelNodeRoute route = getRoute(startNode, endNode, unit);" "A* timed")
require_text("${route}" "            if (endPath.empty())\n            {\n                ai::route_time::PhaseTimer const endLegTimer(routeTimes, ai::route_time::Phase::EndLeg);" "end leg timed to every exit")
require_order("${route}" "std::uint64_t const startLegStartUs" "            newStartPath = startPath;" "start leg timed")
require_text("${route}" "        ai::route_time::PhaseTimer const hearthTimer(routeTimes, ai::route_time::Phase::Hearth);" "hearthstone fallback timed")
count_text("${route}" "NowUs()" route_clocks)
count_text("${route}" "routeTimes ? ai::bot_update::NowUs() : 0" route_gated)
if(NOT route_clocks EQUAL route_gated)
  message(FATAL_ERROR "#592 RouteTime: getRoute reads the clock outside 'routeTimes ? NowUs() : 0' (${route_clocks} vs ${route_gated})")
endif()

# 5. Navmesh steps: counted and timed only inside a timed build.
function_body("${pos_cpp}" "std::vector<WorldPosition> WorldPosition::getPathFromPath(" from_path)
require_text("${from_path}" "ai::route_time::RouteTimes* const routeTimes = ai::route_time::Active();" "active build read once per path")
require_text("${from_path}" "        std::uint64_t const stepStartUs = routeTimes ? ai::bot_update::NowUs() : 0;\n        subPath = getPathStepFrom(currentPos, pathfinder, bot);\n        if (routeTimes)\n            routeTimes->AddStep(ai::bot_update::SinceUs(stepStartUs));" "step timed behind the build")

# 6. M:route: the appendix after pts= (the "M:route [0-9]+us pairs=[0-9]+ pts=[0-9]+" parsers keep matching), empty off.
require_text("${move_cpp}" "traceEngine->TraceNote(\"M:route %lluus pairs=%u pts=%u%s\", (unsigned long long)ai::bot_update::SinceUs(routeStartUs),\n                detail.pairs, uint32(outMovePath.getPath().size()), route_time::FormatToken(detail.times).c_str());" "token appendix")
require_text("${policy}" "    if (!t.on)\n        return std::string();\n    char buf[192];" "no token text when not timed")
# [RouteFailDetail]: the old fields unchanged, the appendix last.
require_text("${move_cpp}" "end_dist=%.0f direct_gap=%.1f no_nodes=%u cooldown=%u budget=%u transport=%u%s\"," "detail appendix at the end")
require_text("${move_cpp}" "std::string const suffix = route_time::FormatDetail(d.times)" "detail appendix from the times")

# 7. dispatch_*: the MovePoint of DispatchMovement, only with generatePath and the switch.
function_body("${move_cpp}" "void MovementAction::DispatchMovement(TravelPath movePath, bool generatePath, bool masterWalking)" dispatch)
require_text("${dispatch}" "bool const timeDispatch = generatePath && sPlayerbotAIConfig.perfRouteTimeTrace;" "dispatch timed only with generatePath and the switch")
require_text("${dispatch}" "std::uint64_t const dispatchStartUs = timeDispatch ? ai::bot_update::NowUs() : 0;" "dispatch clock gated")
require_order("${dispatch}" "dispatchStartUs = timeDispatch" "        mm.MovePoint(movePosition.getMapId()," "clock before the first MovePoint")
require_order("${dispatch}" "        mm.MovePoint(movePosition.getMapId()," "        if (timeDispatch)\n            route_time::Global().AddDispatch(ai::bot_update::SinceUs(dispatchStartUs), route_time::LevelBand(bot->GetLevel()));" "dispatch counted after the MovePoint")
require_order("${dispatch}" "route_time::Global().AddDispatch(" "GeneratePathAvoidingHazards(path);" "only the first MovePoint block (before MovePath)")
# PATH-02 A20 (AiPlayerbot.Perf.DispatchPrecomputedPath): the second count times the MovePoint that takes the built path;
# both are gated by timeDispatch and only one of them runs per dispatch (dispatch_path_source_contract).
count_text("${dispatch}" "AddDispatch(" dispatch_adds)
if(NOT dispatch_adds EQUAL 2)
  message(FATAL_ERROR "#592 RouteTime: expected two dispatch counts in DispatchMovement (MovePoint as before / A20), found ${dispatch_adds}")
endif()
count_text("${dispatch}" "        if (timeDispatch)\n            route_time::Global().AddDispatch(" dispatch_gated_first)
count_text("${dispatch}" "            if (timeDispatch)\n                route_time::Global().AddDispatch(" dispatch_gated_a20)
if(NOT dispatch_gated_first EQUAL 1 OR NOT dispatch_gated_a20 EQUAL 1)
  message(FATAL_ERROR "#592 RouteTime: every dispatch count must be gated by timeDispatch (${dispatch_gated_first} / ${dispatch_gated_a20})")
endif()

# 8. [RouteTime]: gated minute line on the world thread, next to [RouteFail], before the PerfMon Init.
function_body("${rnd_mgr}" "void ReportRouteTime()" report)
require_text("${report}" "if (!sPlayerbotAIConfig.perfRouteTimeTrace || now < lastReport + 60)\n        return;" "nothing when off, once a minute")
require_text("${report}" "ai::route_time::FormatLine(ai::route_time::Global().Take())" "counters taken once")
require_text("${policy}" "\"[RouteTime] builds=%llu ms=%llu ge16=%llu ge33=%llu ge50=%llu ge100=%llu max_ms=%llu\"" "minute line head")
require_text("${policy}" "\" dsteps_ok=\" + FormatSteps(s.dstepsOk) + \" dsteps_fail=\" + FormatSteps(s.dstepsFail) + tail" "step classes")
require_text("${policy}" "\" dispatch_n=%llu dispatch_ms=%llu dispatch_max_us=%llu" "dispatch fields")
function_body("${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd)
require_order("${upd}" "\n    ReportRouteFail();" "\n    ReportRouteTime();" "after [RouteFail]")
require_order("${upd}" "\n    ReportRouteTime();" "sPerformanceMonitor.Init(0, 0);" "report before the PerfMon Init")

message(STATUS "route_time source contract passed")
