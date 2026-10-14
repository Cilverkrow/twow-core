cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet D66 (AiPlayerbot.Diet.HunterRapidFireGate, default 0): the vanilla "rapid fire" trigger
# asks for the known, ready spell and the node drops the "readiness" alternative, both only while the bot does not
# know Readiness. With 0 the trigger is the plain BuffTrigger and the node is ACTION_NODE_A as before.
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
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterTriggers.h" hunter_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterStrategy.cpp" hunter_strategy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" playerbot_ai_cpp)

# Switch: default 0 in member, loader and dist config.
require_text("${config_h}" "bool dietHunterRapidFireGate = false;" "dietHunterRapidFireGate member default false")
require_text("${config_cpp}" "dietHunterRapidFireGate = config.GetBoolDefault(\"AiPlayerbot.Diet.HunterRapidFireGate\", false);" "HunterRapidFireGate read with default 0")
require_text("${config_dist}" "\nAiPlayerbot.Diet.HunterRapidFireGate = 0\n" "HunterRapidFireGate = 0 in the dist config")

# Trigger: vanilla-only, known + ready check behind the switch, Readiness keeps the old path, then the aura check.
region("${hunter_triggers_h}" "class RapidFireTrigger : public BuffTrigger" "\n    };" rapid_trigger)
require_text("${rapid_trigger}" "RapidFireTrigger(PlayerbotAI* ai) : BuffTrigger(ai, \"rapid fire\") {}" "Rapid Fire trigger constructor unchanged")
require_order("${rapid_trigger}" "#ifdef MANGOSBOT_ZERO" "virtual bool IsActive() override" "D66 override inside the vanilla block")
require_order("${rapid_trigger}" "virtual bool IsActive() override" "#endif" "D66 override closed by #endif")
require_text("${rapid_trigger}" "if (sPlayerbotAIConfig.dietHunterRapidFireGate)" "D66 gate behind the switch")
require_text("${rapid_trigger}" "const uint32 spellId = AI_VALUE2(uint32, \"spell id\", spell);" "D66 reads the spell id value the cast uses")
require_text("${rapid_trigger}" "if ((!spellId || !bot->IsSpellReady(spellId)) && !AI_VALUE2(uint32, \"spell id\", \"readiness\"))\n                    return false;" "D66 known/ready gate, Readiness keeps the old path")
require_text("${rapid_trigger}" "return BuffTrigger::IsActive();" "D66 falls through to the old aura check")
require_order("${rapid_trigger}" "dietHunterRapidFireGate" "return BuffTrigger::IsActive();" "D66 gate before the aura check")
forbid_text("${rapid_trigger}" "BOOST_TRIGGER" "D66 must not move Rapid Fire onto the boost balance gate")

# Node factory: one hand-written node (all builds), still a single unconditional new ActionNode (#541 A08 owner
# cache); the "readiness" alternative only with the switch off or a known Readiness, decided at every node build.
# No extra #ifdef in the factory (other contracts take the first "#ifdef MANGOSBOT_ZERO" as the vanilla block).
region("${hunter_strategy}" "class HunterStrategyActionNodeFactory" "\n};" node_factory)
require_text("${node_factory}" "creators[\"rapid fire\"] = &rapid_fire;" "rapid fire node still registered")
region("${node_factory}" "static ActionNode* rapid_fire(PlayerbotAI* ai)" "\n    }\n" rapid_node)
require_text("${rapid_node}" "static ActionNode* rapid_fire(PlayerbotAI* ai)\n    {\n        return new ActionNode(\"rapid fire\"," "D66 hand-written node starts with return new ActionNode")
require_text("${rapid_node}" "/*A*/ (!sPlayerbotAIConfig.dietHunterRapidFireGate || !ai ||\n                   ai->GetAiObjectContext()->GetValue<uint32>(\"spell id\", \"readiness\")->Get())\n                ? NextAction::array(0, new NextAction(\"readiness\"), NULL) : NULL," "D66 readiness alternative only with the switch off or a known Readiness")
require_text("${rapid_node}" "/*P*/ NULL," "D66 node without prerequisites, as the macro")
require_text("${rapid_node}" "/*C*/ NULL);" "D66 node without continuers, as the macro")
forbid_text("${node_factory}" "ACTION_NODE_A(rapid_fire" "a second rapid fire creator")
forbid_text("${node_factory}" "#ifdef" "a build switch inside the node factory")
require_text("${hunter_strategy}" "#include \"playerbot/PlayerbotAIConfig.h\"" "HunterStrategy.cpp includes the config")

# Neutrality anchors: the cast resolves the same spell id value and the same IsSpellReady.
require_text("${playerbot_ai_cpp}" "return CanCastSpell(aiObjectContext->GetValue<uint32>(\"spell id\", name)->Get()" "CanCastSpell(name) resolves the same spell id value")
require_text("${playerbot_ai_cpp}" "if (!bot->IsSpellReady(spellid))" "CanCastSpell refuses a spell on cooldown")
region("${hunter_strategy}" "void HunterBoostStrategy::InitCombatTriggers" "\n}\n" boost_combat)
require_text("${boost_combat}" "\"rapid fire\",\n        NextAction::array(0, new NextAction(\"rapid fire\", ACTION_HIGH + 5), NULL)" "boost node unchanged")

message(STATUS "diet D66 hunter rapid fire gate source contract passed")
