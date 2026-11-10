# twow-repo#575 H4 (A3-HARM-UPSTREAM, OB-00 09.11.2026; bug fix, crash class: code default 0 until the acceptance unit passes, then a flip pair):
# AiPlayerbot.Path.TransportSameMapGuard. MovementAction::UseTransport, not aboard, scanned the vessels of the dock's map
# (WorldPosition::getTransports) before it tested whether the dock lies on the bot's map; a dock on another map belongs to
# another map update thread. With the guard the scan runs only for a dock on the bot's map (TransportSameMapPolicy.h); a dock
# on another map takes the unchanged wait path with no candidate vessel.
# This contract pins the key (dist, member, read, [ConfigEcho] sorted), the one guarded scan before every use of its result,
# that the rest of UseTransport keeps its shape (one scan, the board test, the wait path), the policy rule and the policy test
# registration.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "TransportSameMapGuard: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "TransportSameMapGuard: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(rest "${text}")
  set(n 0)
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${len}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "TransportSameMapGuard: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/TransportSameMapPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" move)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)

# 1. The key (flip pair 10.11.2026, after the acceptance unit H1+H4 u1 passed for H4 and OB-10 OK): dist default 1, member default true,
# read with default true, [ConfigEcho] in sorted place. 0 stays the rollback.
require_text("${dist}" "\nAiPlayerbot.Path.TransportSameMapGuard = 1\n" "dist default 1")
count_text("${dist}" "AiPlayerbot.Path.TransportSameMapGuard =" 1 "one dist line")
require_text("${config_h}" "    bool pathTransportSameMapGuard = true;" "member default true")
require_text("${config_cpp}" "    pathTransportSameMapGuard = config.GetBoolDefault(\"AiPlayerbot.Path.TransportSameMapGuard\", true);" "read with default true")
require_text("${config_cpp}" "            {\"Path.TransportEdgeTrace\", Value(pathTransportEdgeTrace)},\n            {\"Path.TransportSameMapGuard\", Value(pathTransportSameMapGuard)},\n            {\"Path.TransportWaitTimeoutMin\", Value(pathTransportWaitTimeoutMin)}," "[ConfigEcho] sorted")

# 2. The policy: guard off = always the scan; guard on = only on the bot's map.
require_text("${policy}" "inline bool ScanAllowed(bool guard, std::uint32_t dockMap, std::uint32_t botMap)\n{\n    return !guard || dockMap == botMap;\n}" "the rule")

# 3. UseTransport: one guarded scan, after the aboard branch, before the candidate loop and the board / wait paths.
string(FIND "${move}" "bool MovementAction::UseTransport(PlayerbotAI* ai, uint32 entry, WorldPosition dockPosition" use_at)
string(FIND "${move}" "\nbool MovementAction::MinimalMove(" end_at)
if(use_at EQUAL -1 OR end_at EQUAL -1 OR NOT use_at LESS end_at)
  message(FATAL_ERROR "TransportSameMapGuard: UseTransport region not found")
endif()
math(EXPR use_len "${end_at} - ${use_at}")
string(SUBSTRING "${move}" ${use_at} ${use_len} use)
require_text("${move}" "#include \"playerbot/TransportSameMapPolicy.h\"" "policy include")
# OB-10 review 09.11.: with the guard the scan runs on the bot's own map instance (bot->GetMap()). getTransports(entry) takes
# getMap(getFirstInstanceId()), the first instance with that map id - under the region layout (#697) any region of the
# continent, i.e. another pool thread. The legacy call stays only for the guard off.
set(SCAN_GATE "    bool const sameMapGuard = sPlayerbotAIConfig.pathTransportSameMapGuard;\n    bool const scanDock = ai::transport_samemap::ScanAllowed(sameMapGuard, dockPosition.getMapId(), bot->GetMapId());\n    std::set<GenericTransport*> const candidates = !scanDock ? std::set<GenericTransport*>() :   // twow-repo#342: named for cand=\n        sameMapGuard ? dockPosition.getTransports(entry, bot->GetMap()) : dockPosition.getTransports(entry);")
require_text("${use}" "${SCAN_GATE}" "the guarded scan")
count_text("${use}" "getTransports(" 2 "transport scans in UseTransport (guarded on the bot's map, legacy with the guard off)")
count_text("${use}" "getTransports(entry, bot->GetMap())" 1 "the guarded scan on the bot's own map")
count_text("${use}" "ScanAllowed(" 1 "guard calls in UseTransport")

# The map overload: the old body on the given map; the old overload delegates with the old instance choice.
read_source("${PB_SOURCE_DIR}/WorldPosition.cpp" wp_cpp)
read_source("${PB_SOURCE_DIR}/WorldPosition.h" wp_h)
require_text("${wp_h}" "        std::set<GenericTransport*> getTransports(uint32 entry, Map* map);" "map overload declared")
require_text("${wp_cpp}" "std::set<GenericTransport*> WorldPosition::getTransports(uint32 entry)\n{\n    return getTransports(entry, getMap(getFirstInstanceId()));\n}" "the old overload delegates with the old instance")
string(FIND "${wp_cpp}" "std::set<GenericTransport*> WorldPosition::getTransports(uint32 entry, Map* map)\n{" ov_at)
string(FIND "${wp_cpp}" "\nvoid WorldPosition::CalculatePassengerPosition(" ov_end)
if(ov_at EQUAL -1 OR ov_end EQUAL -1 OR NOT ov_at LESS ov_end)
  message(FATAL_ERROR "TransportSameMapGuard: map overload region not found")
endif()
math(EXPR ov_len "${ov_end} - ${ov_at}")
string(SUBSTRING "${wp_cpp}" ${ov_at} ${ov_len} overload)
require_text("${overload}" "    if (!map)\n        return transports;" "no map, no scan")
require_text("${overload}" "    for (auto transport : map->GetTransports()) //Boats&Zeppelins." "vessels of the given map")
count_text("${overload}" "map->GetGameObject(" 2 "elevators of the given map (cache and walk)")
count_text("${overload}" "getFirstInstanceId" 0 "no other instance in the map overload")
count_text("${overload}" "getMap(" 0 "no map lookup in the map overload")
require_order("${use}" "        return false;\n    }\n\n    float minDist = 0;" "${SCAN_GATE}" "aboard branch first (a ride's own vessel, no scan)")
require_order("${use}" "${SCAN_GATE}" "    for (auto& trans : candidates)" "the guard before the candidate loop")
require_order("${use}" "    for (auto& trans : candidates)" "    bool const legacyBoard = transport && dockPosition.mapid == bot->GetMapId()" "board test after the loop, unchanged")
require_text("${use}" "        ai->TellDebug(ai->GetMaster(), \"Waiting for transport on different map.\", \"debug move\");" "the different-map wait stays")
require_text("${use}" "TraceTransportWait(bot, *edge, uint32(candidates.size()), transport ? std::sqrt(minDist) : -1.0f);" "the wait keeps counting")

# 4. The policy test is registered and compiled.
require_text("${tests}" "add_executable(transport_samemap_policy_tests\n  \"\${PB_MODULE_DIR}/t/transport_samemap_policy_tests.cpp\")" "policy test target")
require_text("${tests}" "add_test(NAME transport_samemap_policy\n  COMMAND transport_samemap_policy_tests" "policy test registration")
