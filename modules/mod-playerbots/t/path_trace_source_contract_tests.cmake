cmake_policy(VERSION 3.16)
# twow-repo#541 (L0, measurement only, OB-00 go 11.10.2026): with AiPlayerbot.BotUpdateTrace the engine trace of a
# bot also carries the path sub-phases of MoveTo - "M:cache hit|miss <reason> backDist=<yd>" at the lastPath reuse
# decision, "M:route <us> pairs=<n>" around TravelNodeMap::getFullPath, "M:mmap <us> pts=<n>" around the navmesh
# path, "M:keep last" when a rebuilt path is thrown away for the old one - and "S:engine init <us>" in
# PlayerbotAI::ReInitCurrentEngine. No new switch. Off: no clock read, no entry, the same path as before.
# The live route of MovementAction::MoveTo is MoveTo2 -> ResolveMovePath (MoveTo returns MoveTo2 first; the older
# body below that return is unreachable), so the tokens sit in ResolveMovePath.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 L0: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 L0: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 L0: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 L0: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#541 L0: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
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
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("strategy/Engine.h" engine_h)
read_source("strategy/Engine.cpp" engine_cpp)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("TravelNode.h" node_h)
read_source("TravelNode.cpp" node_cpp)
read_source("PlayerbotAIConfig.cpp" config_cpp)

# 1. The trail entry point: public, same trail as the engine's own entries, nothing with the switch off.
require_text("${engine_h}" "        void TraceNote(const char* format, ...);" "public trace entry")
require_order("${engine_h}" "void TraceNote(const char* format, ...);" "    private:\n        void LogAction(const char* format, ...);" "TraceNote before the private LogAction")
function_body("${engine_cpp}" "void Engine::TraceNote(const char* format, ...)" note_body)
require_order("${note_body}" "if (!sPlayerbotAIConfig.botUpdateTrace)\n        return;" "va_start(ap, format);" "switch checked first")
require_text("${note_body}" "vsnprintf(buf, sizeof(buf), format, ap);" "bounded formatting")
require_text("${note_body}" "LogAction(\"%s\", buf);" "same trail as the engine entries")

# 2. MoveTo's live route is MoveTo2 -> ResolveMovePath.
function_body("${move_cpp}" "bool MovementAction::MoveTo(uint32 mapId, float x, float y, float z, bool idle, bool react, bool noPath, bool ignoreEnemyTargets)" moveto_body)
require_text("${moveto_body}" "{\n    return MoveTo2(WorldPosition(mapId, x, y, z), idle, react, noPath, ignoreEnemyTargets);" "MoveTo hands over to MoveTo2 first")
function_body("${move_cpp}" "bool MovementAction::MoveTo2(const WorldPosition& endPos, bool idle, bool react, bool noPath, bool ignoreEnemyTargets)" moveto2_body)
require_text("${moveto2_body}" "TravelPath movePath = ResolveMovePath(startPos, endPos, mover, lastMove);" "MoveTo2 resolves its path in ResolveMovePath")

# 3. ResolveMovePath: every token and clock behind the switch.
function_body("${move_cpp}" "TravelPath MovementAction::ResolveMovePath(" resolve_body)
require_text("${resolve_body}" "ai::Engine* const traceEngine = sPlayerbotAIConfig.botUpdateTrace ? ai->GetCurrentEngine() : nullptr;" "trace engine only with the switch")
require_order("${resolve_body}" "ai::Engine* const traceEngine" "// Last long path still leads to roughly the same destination." "switch read before the reuse decision")
# The existing reuse decision and the keep-old rule, unchanged.
require_text("${resolve_body}" "if (!lastMove.lastPath.empty() && lastMove.lastPath.getBack().distance(endPosition) < maxDistChange)\n    {\n        if (traceEngine)\n            traceEngine->TraceNote(\"M:cache hit near backDist=%.0f\"" "hit token inside the unchanged reuse decision")
require_order("${resolve_body}" "traceEngine->TraceNote(\"M:cache hit near backDist=%.0f\"" "        return lastMove.lastPath;\n    }" "hit token before the reuse return")
require_text("${resolve_body}" "    if (traceEngine)\n    {\n        if (lastMove.lastPath.empty())\n            traceEngine->TraceNote(\"M:cache miss empty backDist=-1 max=%.0f\", maxDistChange);" "miss token (empty) behind the switch")
require_text("${resolve_body}" "traceEngine->TraceNote(\"M:cache miss %s backDist=%.0f max=%.0f\"," "miss token (map/far)")
require_text("${resolve_body}" "lastMove.lastPath.getBack().getMapId() != endPosition.getMapId() ? \"map\" : \"far\"" "miss reason")
require_text("${resolve_body}" "std::uint64_t const routeStartUs = traceEngine ? ai::bot_update::NowUs() : 0;" "route clock only with the switch")
require_text("${resolve_body}" "outMovePath = sTravelNodeMap.getFullPath(startPosition, endPosition, bot, traceEngine ? &routePairs : nullptr); //Pathfind using nodes." "pair count only with the switch")
require_order("${resolve_body}" "std::uint64_t const routeStartUs" "sTravelNodeMap.getFullPath(" "route clock before the route")
require_order("${resolve_body}" "sTravelNodeMap.getFullPath(" "        if (traceEngine)\n            traceEngine->TraceNote(\"M:route %lluus pairs=%u pts=%u\"" "route token after the route, behind the switch")
require_text("${resolve_body}" "std::uint64_t const mmapStartUs = traceEngine ? ai::bot_update::NowUs() : 0;" "navmesh clock only with the switch")
require_order("${resolve_body}" "std::uint64_t const mmapStartUs" "std::vector<WorldPosition> path = startPosition.getPathTo(endPosition, bot); //Navemesh pathfinding only." "navmesh clock before the path")
require_order("${resolve_body}" "startPosition.getPathTo(endPosition, bot); //Navemesh pathfinding only." "        if (traceEngine)\n            traceEngine->TraceNote(\"M:mmap %lluus pts=%u\"" "navmesh token after the path, behind the switch")
require_text("${resolve_body}" "if (!lastMove.lastPath.empty() && !outMovePath.empty() && lastMove.lastPath.getBack().distance(endPosition) <= outMovePath.getBack().distance(endPosition))\n    {\n        outMovePath = lastMove.lastPath;\n        if (traceEngine)\n            traceEngine->TraceNote(\"M:keep last\");" "keep-old rule unchanged, token behind the switch")
# Every TraceNote call in ResolveMovePath directly behind `if (traceEngine)` (the two miss lines inside one block;
# L1 adds "M:reuse hit" one block deeper, inside its own switch).
count_text("${resolve_body}" "TraceNote(" notes)
count_text("${resolve_body}" "if (traceEngine)\n            traceEngine->TraceNote(" gated)
count_text("${resolve_body}" "if (traceEngine)\n                traceEngine->TraceNote(" gated_deep)
math(EXPR accounted "${gated} + ${gated_deep} + 2")
if(NOT gated EQUAL 4 OR NOT notes EQUAL accounted)
  message(FATAL_ERROR "#541 L0: ResolveMovePath trace entries: ${notes} found, ${gated}+${gated_deep} directly gated + 2 in the gated miss block expected (4 at the L0 sites)")
endif()

# 4. Pair count: optional out parameter, default null, written only when given.
require_text("${node_h}" "std::vector<WorldPosition>& endPath, Unit* unit = nullptr, uint32* outPairs = nullptr);" "getRoute pair out parameter defaults to null")
require_text("${node_h}" "static TravelPath getFullPath(WorldPosition startPos, WorldPosition endPos, Unit* unit = nullptr, uint32* outPairs = nullptr);" "getFullPath pair out parameter defaults to null")
require_text("${node_cpp}" "            ++routePairs;\n            if (outPairs)   // twow-repo#541 (L0): trace only, the caller's local\n                *outPairs = routePairs;" "pair count written only when asked")
require_text("${node_cpp}" "TravelNodeRoute route = sTravelNodeMap.getRoute(startPos, endPos, beginPath, endPath, unit, outPairs);" "getFullPath passes the pair pointer")

# 5. Engine init cost.
function_body("${ai_cpp}" "void PlayerbotAI::ReInitCurrentEngine()" reinit_body)
require_text("${reinit_body}" "bool const traceInit = sPlayerbotAIConfig.botUpdateTrace;" "one switch read")
require_order("${reinit_body}" "std::uint64_t const initStartUs = traceInit ? ai::bot_update::NowUs() : 0;" "    currentEngine->Init();" "init clock only with the switch, before Init")
require_order("${reinit_body}" "    currentEngine->Init();" "    if (traceInit)\n        currentEngine->TraceNote(\"S:engine init %lluus\"" "init token after Init, behind the switch")
require_text("${ai_cpp}" "#include \"playerbot/BotUpdateTrace.h\"" "trace clock header")
require_text("${move_cpp}" "#include \"playerbot/BotUpdateTrace.h\"" "trace clock header")

# 6. No switch of its own.
forbid_text("${config_cpp}" "AiPlayerbot.PathTrace" "own switch (L0 rides on BotUpdateTrace)")
forbid_text("${config_cpp}" "AiPlayerbot.BotUpdateTrace.Path" "own switch (L0 rides on BotUpdateTrace)")

message(STATUS "path_trace source contract passed")
