cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g4 (hunter + generic). Every fix sits behind its own switch
# AiPlayerbot.Diet.<Key>, default 0; with 0 the old trigger/strategy/action path runs unchanged.
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
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterStrategy.cpp" hunter_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterTriggers.cpp" hunter_triggers_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterActions.cpp" hunter_actions_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterActions.h" hunter_actions_h)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterTriggers.h" hunter_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/hunter/HunterAiObjectContext.cpp" hunter_context)

# Only the vanilla block of HunterStrategy.cpp is compiled in this build.
region("${hunter_strategy}" "#ifdef MANGOSBOT_ZERO" "#endif" hunter_zero)
require_text("${hunter_strategy}" "#include \"playerbot/PlayerbotAIConfig.h\"" "HunterStrategy.cpp includes the config")

# Every switch defaults to 0.
foreach(pair
    "dietHunterBestSting|HunterBestSting"
    "dietHunterTalentShotsKnown|HunterTalentShotsKnown"
    "dietHunterTranqKnown|HunterTranqKnown"
    "dietLootRollPending|LootRollPending")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 member)
  list(GET parts 1 key)
  require_text("${config_h}" "bool ${member} = false;" "${member} member default false")
  require_text("${config_cpp}" "${member} = config.GetBoolDefault(\"AiPlayerbot.Diet.${key}\", false);" "${key} read with default 0")
  require_text("${config_dist}" "\nAiPlayerbot.Diet.${key} = 0\n" "${key} = 0 in the dist config")
endforeach()

# ---------------------------------------------------------------------------------------------------------
# D10 (AiPlayerbot.Diet.HunterBestSting): one 'no stings' node, trigger only when a sting can be used,
# serpent sting on mana targets while viper sting is unknown.
region("${hunter_zero}" "void HunterStingStrategy::InitCombatTriggers" "\n}\n" sting_base)
require_text("${sting_base}" "    if (!sPlayerbotAIConfig.dietHunterBestSting)\n    {\n        triggers.push_back(new TriggerNode(\n            \"no stings\",\n            NextAction::array(0, new NextAction(\"viper sting\", ACTION_NORMAL + 5), NULL)));\n    }" "D10 base viper node only with the switch off")
forbid_text("${sting_base}" "\n    triggers.push_back(new TriggerNode(\n        \"no stings\"" "D10 ungated base 'no stings' node")
require_text("${sting_base}" "\"serpent sting on attacker\", ACTION_HIGH + 2" "D10 on-attacker serpent node kept")
require_text("${sting_base}" "\"viper sting on attacker\", ACTION_HIGH + 1" "D10 on-attacker viper node kept")

set(both_stings "    if (sPlayerbotAIConfig.dietHunterBestSting)\n    {\n        triggers.push_back(new TriggerNode(\n            \"no stings\",\n            NextAction::array(0, new NextAction(\"viper sting\", ACTION_NORMAL + 5), new NextAction(\"serpent sting\", ACTION_NORMAL + 4), NULL)));\n        return;\n    }\n\n    triggers.push_back(new TriggerNode(\n        \"no stings\",\n        NextAction::array(0, new NextAction(\"serpent sting\", ACTION_NORMAL + 4), NULL)));")
foreach(part "Pvp" "Pve")
  region("${hunter_zero}" "void HunterSting${part}Strategy::InitCombatTriggers" "\n}\n" sting_part)
  require_text("${sting_part}" "${both_stings}" "D10 ${part}: one node with both stings (switch on), the old serpent node (switch off)")
endforeach()
region("${hunter_zero}" "void HunterStingRaidStrategy::InitCombatTriggers" "\n}\n" sting_raid)
require_text("${sting_raid}" "    if (sPlayerbotAIConfig.dietHunterBestSting)\n    {\n        triggers.push_back(new TriggerNode(\n            \"no stings\",\n            NextAction::array(0, new NextAction(\"viper sting\", ACTION_NORMAL + 5), NULL)));\n    }" "D10 raid: viper node only with the switch on")
forbid_text("${sting_raid}" "serpent sting" "D10 raid gets no serpent sting")

region("${hunter_triggers_cpp}" "bool HunterNoStingsActiveTrigger::IsActive()" "\n}\n" no_stings)
require_order("${no_stings}" "!ai->HasAura(\"viper sting\", target);" "if (!noStings || !sPlayerbotAIConfig.dietHunterBestSting)\n        return noStings;" "D10 old check first, then the switch")
require_order("${no_stings}" "if (!noStings || !sPlayerbotAIConfig.dietHunterBestSting)\n        return noStings;" "AI_VALUE2(uint32, \"spell id\", \"viper sting\")" "D10 spell lookups only with the switch on")
require_text("${no_stings}" "AI_VALUE2(uint8, \"health\", \"current target\") > 40" "D10 old 40 % health check kept")
require_text("${no_stings}" "if (viperKnown && targetHasMana)\n        return true;" "D10 viper case")
require_text("${no_stings}" "return AI_VALUE2(uint8, \"health\", \"current target\") > 50 && (!targetHasMana || !viperKnown) &&\n        AI_VALUE2(uint32, \"spell id\", \"serpent sting\") != 0;" "D10 serpent case mirrors serpent isUseful")

region("${hunter_actions_cpp}" "bool CastSerpentStingAction::isUseful()" "\n}\n" serpent_useful)
require_order("${serpent_useful}" "(!(AI_VALUE2(uint8, \"mana\", GetTargetName()) >= 10) || !AI_VALUE2(uint32, \"spell id\", \"viper sting\"))" "return CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, \"health\", GetTargetName()) > 50 && !(AI_VALUE2(uint8, \"mana\", GetTargetName()) >= 10);" "D10 serpent: switch branch, then the old line")
require_text("${serpent_useful}" "if (sPlayerbotAIConfig.dietHunterBestSting)" "D10 serpent switch")
region("${hunter_actions_cpp}" "bool CastViperStingAction::isUseful()" "\n}\n" viper_useful)
require_order("${viper_useful}" "if (sPlayerbotAIConfig.dietHunterBestSting && !AI_VALUE2(uint32, \"spell id\", \"viper sting\"))\n        return false;" "return CastRangedDebuffSpellAction::isUseful() && AI_VALUE2(uint8, \"mana\", GetTargetName()) >= 10;" "D10 viper: known gate, then the old line")

# ---------------------------------------------------------------------------------------------------------
# D19 (AiPlayerbot.Diet.HunterTalentShotsKnown): deterrence / scatter shot on own known+ready triggers,
# scatter shot on the closest attacker targeting the bot.
region("${hunter_zero}" "void HunterBuffStrategy::InitCombatTriggers" "\n}\n" buff_combat)
require_text("${buff_combat}" "        sPlayerbotAIConfig.dietHunterTalentShotsKnown ? \"deterrence\" : \"target of attacker close\",  // twow-repo#568 (D19)\n        NextAction::array(0, new NextAction(\"deterrence\", ACTION_EMERGENCY + 1), NULL)));" "D19 deterrence node on the switched trigger, same action and relevance")
foreach(part "Pvp" "Pve")
  region("${hunter_zero}" "void HunterCc${part}Strategy::InitCombatTriggers" "\n}\n" cc_part)
  require_text("${cc_part}" "        sPlayerbotAIConfig.dietHunterTalentShotsKnown ? \"scatter shot on closest attacker\" : \"target of attacker close\",  // twow-repo#568 (D19)\n        NextAction::array(0, new NextAction(\"scatter shot on closest attacker targeting me\", ACTION_INTERRUPT + 2), NULL)));" "D19 ${part} scatter node on the switched trigger")
endforeach()
region("${hunter_zero}" "void HunterCcPvpStrategy::InitCombatTriggers" "\n}\n" cc_pvp)
require_text("${cc_pvp}" "        \"target of attacker close\",\n        NextAction::array(0, new NextAction(\"freezing trap in place\", ACTION_INTERRUPT + 3), NULL)));" "D19 pvp freezing trap keeps the shared trigger")

region("${hunter_triggers_h}" "class TalentSpellTargetOfAttackerCloseTrigger : public TargetOfAttackerInRange" "};" talent_trigger)
require_text("${talent_trigger}" ": TargetOfAttackerInRange(ai, name, sPlayerbotAIConfig.tooCloseDistance)" "D19 same distance as TargetOfAttackerClose")
require_order("${talent_trigger}" "if (!spellId || !bot->IsSpellReady(spellId))\n                return false;" "return TargetOfAttackerInRange::IsActive();" "D19 known+ready gate before the attacker lookup")
require_text("${talent_trigger}" "const uint32 spellId = AI_VALUE2(uint32, \"spell id\", spellName);" "D19 spell id lookup")
require_text("${hunter_context}" "creators[\"deterrence\"] = [](PlayerbotAI* ai) { return new TalentSpellTargetOfAttackerCloseTrigger(ai, \"deterrence\", \"deterrence\"); };" "D19 deterrence trigger creator")
require_text("${hunter_context}" "creators[\"scatter shot on closest attacker\"] = [](PlayerbotAI* ai) { return new TalentSpellTargetOfAttackerCloseTrigger(ai, \"scatter shot on closest attacker\", \"scatter shot\"); };" "D19 scatter trigger creator")
require_text("${hunter_context}" "creators[\"scatter shot on closest attacker targeting me\"] = [](PlayerbotAI* ai) -> Action*\n                {\n                    if (sPlayerbotAIConfig.dietHunterTalentShotsKnown)\n                        return new CastScatterShotOnClosestAttackerTargetingMeAction(ai);\n                    return new CastScatterShotAction(ai);\n                };" "D19 scatter action creator: closest attacker with the switch, current target without")
# The target name must be a registered Unit* value (ValueContext.h), else GetTarget reads a null value.
region("${hunter_actions_h}" "class CastScatterShotOnClosestAttackerTargetingMeAction" "};" scatter_action)
require_text("${scatter_action}" "std::string GetTargetName() override { return \"closest attacker targeting me\"; }" "D19 scatter action target value name")
file(READ "${PB_SOURCE_DIR}/strategy/values/ValueContext.h" value_context)
require_text("${value_context}" "creators[\"closest attacker targeting me\"] =" "D19 target value registered")

# ---------------------------------------------------------------------------------------------------------
# D47 (AiPlayerbot.Diet.HunterTranqKnown): the hunter reaction node waits for a known, ready Tranquilizing Shot.
region("${hunter_zero}" "void HunterStrategy::InitReactionTriggers" "\n}\n" hunter_reaction)
require_text("${hunter_reaction}" "        sPlayerbotAIConfig.dietHunterTranqKnown ? \"tranquilizing shot\" : \"dispel enrage\",  // twow-repo#568 (D47)\n        NextAction::array(0, new NextAction(\"tranquilizing shot\", ACTION_INTERRUPT), NULL)));" "D47 reaction node on the switched trigger, same action and relevance")
region("${hunter_triggers_h}" "class TranquilizingShotTrigger : public DispelEnrageOnTargetTrigger" "};" tranq_trigger)
require_text("${tranq_trigger}" "TranquilizingShotTrigger(PlayerbotAI* ai) : DispelEnrageOnTargetTrigger(ai, \"tranquilizing shot\") {}" "D47 trigger name and enrage dispel type")
require_order("${tranq_trigger}" "if (!spellId || !bot->IsSpellReady(spellId))\n                return false;" "return DispelEnrageOnTargetTrigger::IsActive();" "D47 known+ready gate before the aura scan")
require_text("${tranq_trigger}" "const uint32 spellId = AI_VALUE2(uint32, \"spell id\", \"tranquilizing shot\");" "D47 spell id lookup")
require_text("${hunter_context}" "creators[\"tranquilizing shot\"] = [](PlayerbotAI* ai) { return new TranquilizingShotTrigger(ai); };" "D47 trigger creator")
# The generic trigger stays as it is for the switch-off path.
file(READ "${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trigger_context)
require_text("${trigger_context}" "creators[\"dispel enrage\"] = [](PlayerbotAI* ai) { return new DispelEnrageOnTargetTrigger(ai); };" "D47 generic dispel enrage trigger kept")

# ---------------------------------------------------------------------------------------------------------
# D48 (AiPlayerbot.Diet.LootRollPending): 'auto loot roll' only while a group and an open roll exist, 5 s cadence.
file(READ "${PB_SOURCE_DIR}/strategy/generic/LootNonCombatStrategy.cpp" loot_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/LootTriggers.h" loot_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/LootTriggers.cpp" loot_triggers_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/actions/LootRollAction.cpp" loot_roll_action)
region("${loot_strategy}" "void RollStrategy::InitNonCombatTriggers" "\n}\n" roll_nc)
require_text("${roll_nc}" "    triggers.push_back(new TriggerNode(\n        sPlayerbotAIConfig.dietLootRollPending ? \"loot roll pending\" : \"very often\",\n        NextAction::array(0, new NextAction(\"auto loot roll\", 100.0f), NULL)));" "D48 roll node on the switched trigger, same action and relevance")
region("${loot_strategy}" "void RollStrategy::InitCombatTriggers" "\n}\n" roll_c)
require_text("${roll_c}" "RollStrategy::InitNonCombatTriggers(triggers);" "D48 combat engine reuses the non-combat node")
require_text("${loot_strategy}" "#include \"playerbot/PlayerbotAIConfig.h\"" "D48 strategy includes the config")
region("${loot_triggers_h}" "class LootRollPendingTrigger : public Trigger" "};" pending_h)
require_text("${pending_h}" "LootRollPendingTrigger(PlayerbotAI* ai) : Trigger(ai, \"loot roll pending\", 5) {}" "D48 same 5 s interval as very often")
region("${loot_triggers_cpp}" "bool LootRollPendingTrigger::IsActive()" "\n}" pending_cpp)
require_text("${pending_cpp}" "return bot->GetGroup() && !AI_VALUE(LootRollMap, \"active rolls\").empty();" "D48 group and open roll, like isPossible")
require_text("${loot_triggers_cpp}" "#include \"playerbot/strategy/values/LootValues.h\"" "D48 LootRollMap visible")
require_text("${trigger_context}" "creators[\"loot roll pending\"] = [](PlayerbotAI* ai) { return new LootRollPendingTrigger(ai); };" "D48 trigger creator")
require_text("${trigger_context}" "creators[\"very often\"] = [](PlayerbotAI* ai) { return new TimeTrigger(ai, \"very often\", 5); };" "D48 very often unchanged (5 s)")
# The action keeps its full guard (group, open roll, bag space).
require_text("${loot_roll_action}" "return bot->GetGroup() && !AI_VALUE(LootRollMap, \"active rolls\").empty() && AI_VALUE(uint8, \"bag space\") < 100;" "D48 isPossible unchanged")

message(STATUS "DIET_G4_HUNTER_GENERIC_SOURCE_CONTRACT=PASS")
