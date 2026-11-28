# twow-repo#790 (S3b-2 step 3, diagnosis, OB-10 OK 28.11.2026): AiPlayerbot.Path.TransportTeleportTrace (default 0, log only).
# ABBA-1 dropped 11 of 198 boarded bots at their vessel's map change, all at the map-entry point. The trace writes one
# [TransportTeleport] ack line per far teleport ack of a bot on a transport and one [TransportTeleport] felloff line per fell-off
# removal. This contract pins: the key (dist 0, member false, read false, [ConfigEcho] sorted), that both hooks are gated by the
# key, that the ack hook only reads before HandleMoveWorldportAckOpcode and logs after NoteTeleportAck, that the fell-off hook
# sits before RemovePassenger, and that the trace bodies only read and log (no movement, passenger or teleport call).

cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "TransportTeleportTrace: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "TransportTeleportTrace: order ${description}: '${first}' must come before '${second}'")
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
    message(FATAL_ERROR "TransportTeleportTrace: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

function(region text start_marker end_marker out_var)
  string(FIND "${text}" "${start_marker}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "TransportTeleportTrace: region start not found: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${end_marker}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "TransportTeleportTrace: region end not found: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)

# 1. The key: off by default everywhere, read once, echoed in sorted place.
require_text("${dist}" "\nAiPlayerbot.Path.TransportTeleportTrace = 0\n" "dist default 0")
count_text("${dist}" "AiPlayerbot.Path.TransportTeleportTrace =" 1 "one dist line")
require_text("${config_h}" "    bool pathTransportTeleportTrace = false;" "member default false")
count_text("${config_cpp}" "\"AiPlayerbot.Path.TransportTeleportTrace\"" 1 "key read once")
require_text("${config_cpp}" "    pathTransportTeleportTrace = config.GetBoolDefault(\"AiPlayerbot.Path.TransportTeleportTrace\", false);" "read with default false")
require_text("${config_cpp}" "            {\"Path.TransportSameMapGuard\", Value(pathTransportSameMapGuard)},\n            {\"Path.TransportTeleportTrace\", Value(pathTransportTeleportTrace)},\n            {\"Path.TransportWaitTimeoutMin\", Value(pathTransportWaitTimeoutMin)}," "[ConfigEcho] sorted")

# 2. Declarations.
require_text("${ai_h}" "    void TraceTransportTeleportAck(uint32 entry, uint32 destMap, float destX, float destY);" "ack trace declared")
require_text("${ai_h}" "    void TraceTransportTeleportFellOff();" "fell-off trace declared")
require_text("${ai_h}" "    uint32 transportFarAckMs = 0;" "ack time member")

# 3. The far ack: the key decides whether anything is read; the R9-2 pair HandleMoveWorldportAckOpcode / NoteTeleportAck stays
# adjacent; the line comes after it, only for a bot that was on a transport.
region("${ai_cpp}" "void PlayerbotAI::HandleTeleportAck()" "\nvoid PlayerbotAI::TraceTransportTeleportAck(" ack)
require_text("${ack}" "        Transport const* const traceTransport = sPlayerbotAIConfig.pathTransportTeleportTrace ? bot->GetTransport() : nullptr;" "ack read gated by the key")
require_text("${ack}" "        bot->GetSession()->HandleMoveWorldportAckOpcode();\n        NoteTeleportAck();" "the far ack and its R9-2 note stay adjacent")
require_text("${ack}" "        if (traceTransport)\n            TraceTransportTeleportAck(traceEntry, traceDestMap, traceDestX, traceDestY);" "ack line only for a passenger")
require_order("${ack}" "bot->GetTeleportDest().mapId" "HandleMoveWorldportAckOpcode();" "destination read before the ack")
require_order("${ack}" "NoteTeleportAck();" "TraceTransportTeleportAck(traceEntry" "line after the ack")
count_text("${ai_cpp}" "TraceTransportTeleportAck(" 2 "ack trace: definition + one call")

# 4. The fell-off branch: gated, before the bot leaves the vessel; the branch keeps its removal.
region("${ai_cpp}" "    // twow-repo#342 (S3b-1, AiPlayerbot.Path.TransportBoardFix): a bot with a ride record (boarded by the fix) stands on the" "        bot->StopMoving();\n    }" fell)
require_text("${fell}" "        if (sPlayerbotAIConfig.pathTransportTeleportTrace)   // twow-repo#790 (S3b-2 step 3): why the fell-off test failed, log only\n            TraceTransportTeleportFellOff();" "fell-off hook gated")
require_order("${fell}" "TraceTransportTeleportFellOff();" "bot->GetTransport()->RemovePassenger(bot);" "line before the removal")
count_text("${fell}" "bot->GetTransport()->RemovePassenger(bot);" 1 "one removal in the branch")
count_text("${ai_cpp}" "TraceTransportTeleportFellOff()" 2 "fell-off trace: definition + one call")

# 5. The trace bodies only read and log.
region("${ai_cpp}" "\nvoid PlayerbotAI::TraceTransportTeleportAck(" "\nvoid PlayerbotAI::Reset(bool full)" bodies)
require_text("${bodies}" "[TransportTeleport] ack bot=" "ack line")
require_text("${bodies}" "[TransportTeleport] felloff bot=" "felloff line")
foreach(word "RemovePassenger(" "AddPassenger(" "TeleportTo(" "NearTeleportTo(" "Relocate(" "StopMoving(" "MoveTo(" "SetStatus(" "Reset(")
  count_text("${bodies}" "${word}" 0 "no state change in the trace bodies: ${word}")
endforeach()
# The two isOnTransport reads of the trace live in its body only (transport_board_fix counts 2 checks + these 2).
count_text("${bodies}" "isOnTransport(" 2 "the trace's two on-transport reads")
