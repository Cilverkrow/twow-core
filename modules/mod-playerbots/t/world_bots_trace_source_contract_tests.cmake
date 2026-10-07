# twow-repo#541 (owner 07.10.2026, OB-00 go option A): [WorldBots] - one line per minute with what the
# bot manager costs the serial world thread. Log only, behind AiPlayerbot.WorldBotsTrace (default 0);
# the pure window is tested in world_bots_trace_policy_tests.cpp.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.h" mgr_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotMgr.cpp" holder)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)

# Switch, default off.
require_text("${config_h}" "bool worldBotsTrace = false;" "WorldBotsTrace member")
require_text("${config_cpp}" "config.GetBoolDefault(\"AiPlayerbot.WorldBotsTrace\", false)" "WorldBotsTrace default off")
require_text("${config_dist}" "AiPlayerbot.WorldBotsTrace = 0" "WorldBotsTrace documented off")

# The parts are timed around the real calls, and the line is written only with the switch.
require_order("${mgr}" "WorldBotsClock::time_point const sessionsStart = WorldBotsClock::now();" "UpdateSessions(elapsed);" "sessions timed from before the call")
require_order("${mgr}" "UpdateSessions(elapsed);" "WorldBotsClock::time_point const sessionsEnd = WorldBotsClock::now();" "sessions timed until after the call")
require_order("${mgr}" "WorldBotsClock::time_point const processStart = WorldBotsClock::now();" "for (uint32 bot : desiredBots)" "ProcessBot loop timed")
require_text("${mgr}" "if (sPlayerbotAIConfig.worldBotsTrace)\n            TraceWorldBots(" "line only with the switch")
require_text("${mgr}" "[WorldBots] passes=%u bots_avg=%u sessions_ms=%u process_ms=%u other_ms=%u max_pass_ms=%u share_pm=%u" "WorldBots line")
require_text("${mgr_h}" "ai::world_bots::Window worldBotsWindow;" "per-minute window")
require_text("${holder}" "GetBotAI(bot)->HandleTeleportAck();\n            ++teleportAcks;" "teleport ACK calls counted")

message(STATUS "WORLD_BOTS_TRACE_SOURCE_CONTRACT=PASS")
