cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet D68 (AiPlayerbot.Diet.RpgDeadTriggers, default 0): "rpg ai chat", "rpg wander" and
# "rpg quest use" answer from the first condition of their IsActive before the RpgTrigger pre-check values. With 0
# every Check() is RpgTrigger::Check as before; a set event flag always runs the old path.
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
file(READ "${PB_SOURCE_DIR}/strategy/triggers/RpgTriggers.h" rpg_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/RpgTriggers.cpp" rpg_triggers_cpp)

# Switch: default 0 in member, loader and dist config.
require_text("${config_h}" "bool dietRpgDeadTriggers = false;" "dietRpgDeadTriggers member default false")
require_text("${config_cpp}" "dietRpgDeadTriggers = config.GetBoolDefault(\"AiPlayerbot.Diet.RpgDeadTriggers\", false);" "RpgDeadTriggers read with default 0")
require_text("${config_dist}" "\nAiPlayerbot.Diet.RpgDeadTriggers = 0\n" "RpgDeadTriggers = 0 in the dist config")

# Declarations on exactly the three triggers.
region("${rpg_triggers_h}" "class RpgWanderTrigger : public RpgTrigger" "\n    };" wander_h)
require_text("${wander_h}" "virtual Event Check() override;" "RpgWanderTrigger::Check declared")
require_text("${wander_h}" "virtual bool IsActive() override { return ai->HasRealPlayerMaster() &&" "RpgWanderTrigger::IsActive starts with HasRealPlayerMaster")
region("${rpg_triggers_h}" "class RpgQuestUseTrigger : public RpgUseTrigger" "\n    };" quest_use_h)
require_text("${quest_use_h}" "virtual Event Check() override;" "RpgQuestUseTrigger::Check declared")
region("${rpg_triggers_h}" "class RpgAIChatTrigger : public RpgTrigger" "\n    };" ai_chat_h)
require_text("${ai_chat_h}" "virtual Event Check() override;" "RpgAIChatTrigger::Check declared")
region("${rpg_triggers_h}" "class RpgTrigger : public FarFromRpgTargetTrigger" "\n    };" rpg_trigger_h)
forbid_text("${rpg_triggers_h}" "class RpgUseTrigger : public RpgTrigger\n    {\n    public:\n        RpgUseTrigger(PlayerbotAI* ai, std::string name = \"rpg use\") : RpgTrigger(ai, name) {}\n        virtual bool IsActive() override;\n        virtual Event Check()" "D68 must not change the shared 'rpg use' trigger")
forbid_text("${rpg_trigger_h}" "dietRpgDeadTriggers" "D68 must not change the RpgTrigger base")

# rpg wander: short cut only without the event flag and without a real master, then the old Check.
region("${rpg_triggers_cpp}" "Event RpgWanderTrigger::Check()" "\n}\n" wander_check)
require_text("${wander_check}" "if (sPlayerbotAIConfig.dietRpgDeadTriggers && !triggered && !ai->HasRealPlayerMaster())\n        return Event();" "D68 rpg wander short cut")
require_text("${wander_check}" "return RpgTrigger::Check();" "D68 rpg wander falls back to the old Check")

# rpg ai chat: short cut while LLMEnabled is 0 (read every check), then the old Check; IsActive still starts there.
region("${rpg_triggers_cpp}" "Event RpgAIChatTrigger::Check()" "\n}\n" ai_chat_check)
require_text("${ai_chat_check}" "if (sPlayerbotAIConfig.dietRpgDeadTriggers && !triggered && sPlayerbotAIConfig.llmEnabled == 0)\n        return Event();" "D68 rpg ai chat short cut")
require_text("${ai_chat_check}" "return RpgTrigger::Check();" "D68 rpg ai chat falls back to the old Check")
region("${rpg_triggers_cpp}" "bool RpgAIChatTrigger::IsActive()" "\n};\n" ai_chat_active)
require_order("${ai_chat_active}" "if (sPlayerbotAIConfig.llmEnabled == 0)\n        return false;" "urand(0, 99)" "LLMEnabled = 0 answers before the chance roll")

# rpg quest use: entry test first in IsActive and Check, old body after it.
region("${rpg_triggers_cpp}" "bool RpgQuestUseTrigger::IsActive()" "\n}\n" quest_use_active)
set(entry_gate "if (sPlayerbotAIConfig.dietRpgDeadTriggers && getGuidP().GetEntry() != 190767)\n        return false;")
require_text("${quest_use_active}" "${entry_gate}" "D68 quest use entry test behind the switch")
require_order("${quest_use_active}" "${entry_gate}" "if (!RpgUseTrigger::IsActive())" "D68 entry test before the game object lookups")
require_text("${quest_use_active}" "case 190767: return AI_VALUE2(bool, \"need quest objective\", \"12701\");" "quest use only true case unchanged")
region("${rpg_triggers_cpp}" "Event RpgQuestUseTrigger::Check()" "\n}\n" quest_use_check)
require_text("${quest_use_check}" "if (sPlayerbotAIConfig.dietRpgDeadTriggers && !triggered && getGuidP().GetEntry() != 190767)\n        return Event();" "D68 quest use Check short cut")
require_text("${quest_use_check}" "return RpgUseTrigger::Check();" "D68 quest use falls back to the old Check")

message(STATUS "diet D68 rpg dead triggers source contract passed")
