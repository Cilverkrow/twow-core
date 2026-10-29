# twow-repo#342 ABBA-1 §6 (OB-00 28.10.2026): the PosStuck guard (Path.PosStuck) pauses while the bot waits at its dock for a
# ship or zeppelin. The dock wait runs up to Path.TransportWaitTimeoutMin (15 min), beyond the 10 min window, and a detect
# parked or moved a bot that only waited (ABBA-1: PosStuck detects within 40 yd of a dock in both arms). HandleSpecialMovement
# stamps LastMovement::transportWaitMs on every wait (both transport branches, the transport not used and the bot not
# aboard); ObservePosStuck excludes the bot for DockWaitPauseMs after the last stamp, so the window starts fresh after the
# ride. The rule is tested in t/pos_stuck_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#342 PosStuck dock pause: missing ${description}: ${needle}")
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
    message(FATAL_ERROR "#342 PosStuck dock pause: ${description} (found ${found}, expected ${expected}): ${needle}")
  endif()
endfunction()

read_source("PosStuckPolicy.h" policy)
read_source("TravelMgr.cpp" travel_cpp)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("strategy/values/LastMovementValue.h" last_h)

# The rule: 30 s after the last dock wait, wrapping clock, 0 = never waited.
require_text("${policy}" "constexpr std::uint32_t DockWaitPauseMs = 30000;" "the pause length")
require_text("${policy}" "    return waitMs != 0 && std::uint32_t(nowMs - waitMs) < pauseMs;" "the pause rule")

# The stamp: in both transport branches of HandleSpecialMovement, only while the transport is not used and the bot not aboard.
set(stamp "        if (!usedTransport && !bot->GetTransport())   // twow-repo#342 ABBA-1 §6: the dock wait pauses PosStuck (PosStuckPolicy.h)\n            AI_VALUE(LastMovement&, \"last movement\").transportWaitMs = WorldTimer::getMSTime();")
require_count("${move_cpp}" "${stamp}" 2 "the dock wait stamp in both transport branches")
require_text("${move_cpp}" "            LogTransportWait(bot, nextPoint.entry, nextPoint.point);\n${stamp}" "the stamp where the bot gets on (next point a transport)")
require_text("${move_cpp}" "            LogTransportWait(bot, currentPoint.entry, currentPoint.point);\n${stamp}" "the stamp where the bot stands on the transport node")

# The field survives the copy of LastMovement and is not cleared by clear() (the pause lapses by itself).
require_text("${last_h}" "        uint32 transportWaitMs = 0;" "the field")
require_text("${last_h}" "            transportWaitMs = other.transportWaitMs;" "the copy")
string(FIND "${last_h}" "void clear()" clear_at)
string(FIND "${last_h}" "void Set(Unit* lastFollow)" clear_end)
math(EXPR clear_len "${clear_end} - ${clear_at}")
string(SUBSTRING "${last_h}" ${clear_at} ${clear_len} clear_body)
string(FIND "${clear_body}" "transportWaitMs" cleared)
if(NOT cleared EQUAL -1)
  message(FATAL_ERROR "#342 PosStuck dock pause: clear() must not reset transportWaitMs (a path reset at the dock would end the pause)")
endif()

# OB-10 review: the pause has an upper limit - Path.TransportWaitTimeoutMin of continuous waiting, counted from the start of the
# continuous pause (State::dockPauseSince, not reset by Reset()), then the window runs again.
require_text("${policy}" "    if (!pausedNow)\n    {\n        since = 0;\n        return false;\n    }\n    if (!since)\n        since = nowMs ? nowMs : 0xFFFFFFFFu;\n    return std::uint32_t(nowMs - since) < maxMs;" "the pause limit rule")
require_text("${policy}" "    std::uint32_t dockPauseSince = 0;\n\n    void Reset() { initialized = false; }" "the pause start in State, kept by Reset()")

# The exclusion: after the population expression, before the distance; evaluated on every observation (the pause start stays
# current even while another exclusion holds), limited by the transport wait timeout.
require_text("${travel_cpp}" "    bool const dockPause = pos_stuck::PauseHolds(posStuck.dockPauseSince, pos_stuck::DockWaitPaused(observation.now,\n            ai->GetAiObjectContext()->GetValue<LastMovement&>(\"last movement\")->Get().transportWaitMs, pos_stuck::DockWaitPauseMs),\n        observation.now, transport_board::WaitTimeoutMsOf(sPlayerbotAIConfig.pathTransportWaitTimeoutMin));\n    observation.excluded = observation.excluded || dockPause;\n    observation.distance = observation.excluded ? 0.0f : Distance(bot);" "the limited dock pause in ObservePosStuck")
