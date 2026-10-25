# twow-repo#594 R1 (OB-10 anti-stall map, OB-00 25.10.2026): AiPlayerbot.Path.PosStuck.LongStuckGridOnly (default 0). With Path.PosStuck
# the upstream "move long stuck" trigger keeps only its invalid / unloaded grid cases, so its hearthstone / repop no longer undercuts the
# PosStuck chain (park -> local unstuck -> home bind) on a bot that does not move.

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
    message(FATAL_ERROR "#594 R1 long stuck: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#594 R1 long stuck: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/triggers/StuckTriggers.h" stuck)

require_text("${config_h}" "bool pathPosStuckLongStuckGridOnly = false;" "member default off")
require_text("${config_cpp}" "pathPosStuckLongStuckGridOnly = config.GetBoolDefault(\"AiPlayerbot.Path.PosStuck.LongStuckGridOnly\", false);" "config default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Path.PosStuck.LongStuckGridOnly = 0\n" "documented as 0")

# The gate sits in MoveLongStuckTrigger::IsActive: after both grid checks (they stay), before the position / XP checks (PosStuck's case);
# both switches are needed.
string(FIND "${stuck}" "class MoveLongStuckTrigger : public Trigger" a)
string(FIND "${stuck}" "class CombatStuckTrigger : public Trigger" b)
if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
  message(FATAL_ERROR "#594 R1 long stuck: MoveLongStuckTrigger not found before CombatStuckTrigger")
endif()
math(EXPR len "${b} - ${a}")
string(SUBSTRING "${stuck}" ${a} ${len} trigger)
set(gate "            if (sPlayerbotAIConfig.pathPosStuck && sPlayerbotAIConfig.pathPosStuckLongStuckGridOnly)\n                return false;")
require_text("${trigger}" "${gate}" "the gate needs both switches")
require_order("${trigger}" "Stuck: In invalid grid" "${gate}" "invalid grid case kept before the gate")
require_order("${trigger}" "Stuck: In unloaded grid" "${gate}" "unloaded grid case kept before the gate")
require_order("${trigger}" "${gate}" "uint32 timeSinceLastMove = AI_VALUE2(uint32, \"time since last change\", \"current position\");" "gate before the position check")
require_order("${trigger}" "${gate}" "uint32 distanceMoved = AI_VALUE2(uint32, \"distance moved since\", 15 * MINUTE);" "gate before the distance check")

message(STATUS "#594 R1 long stuck source contract passed")
