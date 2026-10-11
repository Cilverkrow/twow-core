cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet D63 (AiPlayerbot.Perf.EventTriggerSkip, default 0): Engine::ProcessTriggers skips the
# Check() of event-only triggers (chat command, world packet) while their event flag is unset. With 0 every trigger
# is polled as before; with 1 a set flag is still checked and handled on the same pass.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(READ "${PB_SOURCE_DIR}/strategy/Trigger.h" trigger_h)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/ChatCommandTrigger.h" chat_trigger_h)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/WorldPacketTrigger.h" packet_trigger_h)
file(READ "${PB_SOURCE_DIR}/strategy/Engine.cpp" engine_cpp)

# Switch: default 0 in member, loader and dist config.
require_text("${config_h}" "bool perfEventTriggerSkip = false;" "perfEventTriggerSkip member default false")
require_text("${config_cpp}" "perfEventTriggerSkip = config.GetBoolDefault(\"AiPlayerbot.Perf.EventTriggerSkip\", false);" "EventTriggerSkip read with default 0")
require_text("${config_dist}" "\nAiPlayerbot.Perf.EventTriggerSkip = 0\n" "EventTriggerSkip = 0 in the dist config")

# Trigger base: not event-only by default, IsAlreadyTriggered still reports the base flag.
require_text("${trigger_h}" "virtual bool IsEventOnly() { return false; }" "Trigger::IsEventOnly default false")
require_text("${trigger_h}" "virtual bool IsAlreadyTriggered() { return triggered; }" "Trigger::IsAlreadyTriggered on the base flag")

# Chat commands: event-only, Check() still answers Event() while the base flag is unset.
region("${chat_trigger_h}" "class ChatCommandTrigger" "\n    };" chat_class)
require_text("${chat_class}" "virtual bool IsEventOnly() override { return true; }" "ChatCommandTrigger is event-only")
require_text("${chat_class}" "if (!triggered)\n                return Event();" "ChatCommandTrigger::Check returns Event() without an event")
require_text("${chat_class}" "triggered = true;" "ChatCommandTrigger::ExternalEvent sets the base flag")
forbid_text("${chat_class}" "IsActive" "ChatCommandTrigger never evaluates IsActive")

# World packets: event-only, IsAlreadyTriggered reports the shadowing flag that Check() reads; the shadow stays.
region("${packet_trigger_h}" "class WorldPacketTrigger" "\n    };" packet_class)
require_text("${packet_class}" "virtual bool IsEventOnly() override { return true; }" "WorldPacketTrigger is event-only")
require_text("${packet_class}" "virtual bool IsAlreadyTriggered() override { return triggered; }" "WorldPacketTrigger reports its own flag")
require_text("${packet_class}" "        bool triggered;\n" "WorldPacketTrigger keeps the shadowing flag (ExternalEventForce must not fire it)")
require_text("${packet_class}" "if (!triggered)\n                return Event();" "WorldPacketTrigger::Check returns Event() without a packet")
forbid_text("${packet_class}" "IsActive" "WorldPacketTrigger never evaluates IsActive")

# Engine: the skip sits in ProcessTriggers after the trigger lookup and before the poll; reset loop unchanged.
region("${engine_cpp}" "void Engine::ProcessTriggers(bool minimal)" "\nvoid Engine::PushDefaultActions()" process_triggers)
set(skip_line "if (sPlayerbotAIConfig.perfEventTriggerSkip && !testMode && trigger->IsEventOnly() && !trigger->IsAlreadyTriggered())\n            continue;")
require_text("${process_triggers}" "${skip_line}" "D63 skip of an unset event-only trigger behind the switch")
require_order("${process_triggers}" "if (!trigger)\n            continue;" "${skip_line}" "D63 skip after the trigger lookup")
require_order("${process_triggers}" "${skip_line}" "if (testMode || trigger->IsAlreadyTriggered() || trigger->needCheck())" "D63 skip before the poll")
require_text("${process_triggers}" "if (testMode || trigger->IsAlreadyTriggered() || trigger->needCheck())" "old poll condition unchanged")
require_text("${process_triggers}" "Event event = trigger->Check();" "poll still calls Check()")
require_text("${process_triggers}" "if (trigger) trigger->Reset();" "every trigger still reset after the pass")
string(REGEX MATCH "[^A-Za-z_]return[ ;(]" early_return "${process_triggers}")
if(early_return)
  message(FATAL_ERROR "D63: no return statement in ProcessTriggers (the reset loop must run)")
endif()
string(REGEX MATCHALL "IsEventOnly" event_only_uses "${process_triggers}")
list(LENGTH event_only_uses event_only_count)
if(NOT event_only_count EQUAL 1)
  message(FATAL_ERROR "D63: ProcessTriggers must ask IsEventOnly exactly once, found ${event_only_count}")
endif()

message(STATUS "diet D63 event trigger skip source contract passed")
