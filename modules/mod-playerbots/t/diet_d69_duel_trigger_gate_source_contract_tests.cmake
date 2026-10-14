cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet D69 (AiPlayerbot.Diet.DuelTriggerGate, default 0): the duel strategy pushes "attack duel
# opponent" from "duel no attackers" (only with a duel) instead of "no attackers". With 0 the node uses "no attackers"
# as before; the "no attackers" trigger itself stays unchanged.
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
file(READ "${PB_SOURCE_DIR}/strategy/generic/DuelStrategy.cpp" duel_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/GenericTriggers.h" generic_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/GenericTriggers.cpp" generic_triggers_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trigger_context)
file(READ "${PB_SOURCE_DIR}/strategy/actions/AttackAction.cpp" attack_action)
file(READ "${PB_SOURCE_DIR}/strategy/values/DuelTargetValue.cpp" duel_target_value)

# Switch: default 0 in member, loader and dist config.
require_text("${config_h}" "bool dietDuelTriggerGate = false;" "dietDuelTriggerGate member default false")
require_text("${config_cpp}" "dietDuelTriggerGate = config.GetBoolDefault(\"AiPlayerbot.Diet.DuelTriggerGate\", false);" "DuelTriggerGate read with default 0")
require_text("${config_dist}" "\nAiPlayerbot.Diet.DuelTriggerGate = 0\n" "DuelTriggerGate = 0 in the dist config")

# Strategy: the node picks its trigger by the switch; action and relevance unchanged; duel requested unchanged.
region("${duel_strategy}" "void DuelStrategy::InitNonCombatTriggers" "\n}\n" duel_nc)
require_text("${duel_strategy}" "#include \"playerbot/PlayerbotAIConfig.h\"" "DuelStrategy.cpp includes the config")
require_text("${duel_nc}" "sPlayerbotAIConfig.dietDuelTriggerGate ? \"duel no attackers\" : \"no attackers\",\n        NextAction::array(0, new NextAction(\"attack duel opponent\", 70.0f), NULL)" "D69 node trigger chosen by the switch")
require_text("${duel_nc}" "\"duel requested\",\n        NextAction::array(0, new NextAction(\"accept duel\", relevance), NULL)" "duel requested node unchanged")
region("${duel_strategy}" "void DuelStrategy::InitCombatTriggers" "\n}" duel_combat)
require_text("${duel_combat}" "InitNonCombatTriggers(triggers);" "combat engine reuses the non-combat nodes")

# Trigger: m_duel first, then the unchanged NoAttackersTrigger check; registered under its own name.
region("${generic_triggers_h}" "class DuelNoAttackersTrigger : public NoAttackersTrigger" "\n    };" duel_trigger_h)
require_text("${duel_trigger_h}" "DuelNoAttackersTrigger(PlayerbotAI* ai) : NoAttackersTrigger(ai, \"duel no attackers\") {}" "D69 trigger name")
region("${generic_triggers_cpp}" "bool DuelNoAttackersTrigger::IsActive()" "\n}\n" duel_trigger_cpp)
require_text("${duel_trigger_cpp}" "return bot->m_duel && NoAttackersTrigger::IsActive();" "D69 duel check before the no attackers check")
region("${generic_triggers_cpp}" "bool NoAttackersTrigger::IsActive()" "\n}\n" no_attackers_cpp)
require_text("${no_attackers_cpp}" "return !AI_VALUE(Unit*, \"current target\") && AI_VALUE(uint8, \"my attacker count\") > 0;" "no attackers trigger unchanged")
forbid_text("${no_attackers_cpp}" "m_duel" "D69 must not change the no attackers trigger")
require_text("${trigger_context}" "creators[\"duel no attackers\"] = [](PlayerbotAI* ai) { return new DuelNoAttackersTrigger(ai); };" "D69 trigger registered")
require_text("${trigger_context}" "creators[\"no attackers\"] = [](PlayerbotAI* ai) { return new NoAttackersTrigger(ai); };" "no attackers registration unchanged")

# Neutrality anchors: the action is only useful with a duel target, which needs m_duel.
region("${attack_action}" "bool AttackDuelOpponentAction::isUseful()" "\n}\n" duel_useful)
require_text("${duel_useful}" "return AI_VALUE(Unit*, \"duel target\");" "attack duel opponent isUseful is the duel target")
require_text("${duel_target_value}" "if (!bot->m_duel || bot->m_duel->opponent.IsEmpty()) return nullptr;" "duel target is null without m_duel")

message(STATUS "diet D69 duel trigger gate source contract passed")
