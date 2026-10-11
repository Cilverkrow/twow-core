cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g2 (priest, mage, warlock). Every fix sits behind its own switch
# AiPlayerbot.Diet.<Key>, default 0; with 0 the old trigger/strategy path runs unchanged.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "CORE_SOURCE_DIR is required")
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

# A node registered only under a strategy-level gate (4-space body, 8-space push), and never ungated.
function(require_gated_node text gate trigger action relevance description)
  set(gated "    if (${gate})\n    {\n        triggers.push_back(new TriggerNode(\n            \"${trigger}\",\n            NextAction::array(0, new NextAction(\"${action}\", ${relevance}), NULL)));\n    }")
  require_text("${text}" "${gated}" "${description}: gated node (switch off keeps it)")
  set(ungated "\n    triggers.push_back(new TriggerNode(\n        \"${trigger}\",\n        NextAction::array(0, new NextAction(\"${action}\"")
  forbid_text("${text}" "${ungated}" "${description}: ungated node")
endfunction()

# A trigger class whose IsActive first asks the switch, then runs the old base IsActive unchanged.
function(require_switch_first_trigger text clazz base spell switch_member description)
  region("${text}" "class ${clazz} : public ${base}" "};" body)
  require_text("${body}" "${clazz}(PlayerbotAI* ai) : ${base}(ai, \"${spell}\")" "${description}: same spell name")
  require_text("${body}" "if (sPlayerbotAIConfig.${switch_member} && !AI_VALUE2(uint32, \"spell id\", spell))\n                return false;\n            return ${base}::IsActive();" "${description}: switch + spell id gate, then the old IsActive")
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(READ "${CORE_SOURCE_DIR}/src/game/SharedDefines.h" shared_defines)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trigger_context)

# Every switch defaults to 0.
foreach(pair
    "dietPriestRacials|PriestRacials"
    "dietPriestSelfBuffKnown|PriestSelfBuffKnown"
    "dietShadowPriestDeadNodes|ShadowPriestDeadNodes"
    "dietWarlockCcKnown|WarlockCcKnown"
    "dietWarlockTalentKnown|WarlockTalentKnown"
    "dietWarlockAmplifyCurse|WarlockAmplifyCurse"
    "dietMageArmorKnown|MageArmorKnown")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 member)
  list(GET parts 1 key)
  require_text("${config_h}" "bool ${member} = false;" "${member} member default false")
  require_text("${config_cpp}" "${member} = config.GetBoolDefault(\"AiPlayerbot.Diet.${key}\", false);" "${key} read with default 0")
  require_text("${config_dist}" "\nAiPlayerbot.Diet.${key} = 0\n" "${key} = 0 in the dist config")
endforeach()

# ---------------------------------------------------------------------------------------------------------
# D20 (AiPlayerbot.Diet.PriestRacials): priest race spells only for races that can learn them.
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestStrategy.h" priest_strategy_h)
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestStrategy.cpp" priest_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/priest/ShadowPriestStrategy.cpp" shadow_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestTriggers.h" priest_triggers_h)

# Race masks = 1 << (race - 1) for the core's race ids (skill_line_ability.race_mask 16/128/8/5).
foreach(race "RACE_HUMAN|1" "RACE_DWARF|3" "RACE_NIGHTELF|4" "RACE_UNDEAD|5" "RACE_TROLL|8")
  string(REPLACE "|" ";" parts "${race}")
  list(GET parts 0 race_name)
  list(GET parts 1 race_id)
  string(REGEX MATCH "${race_name} +=  *${race_id}," race_match "${shared_defines}")
  if(NOT race_match)
    message(FATAL_ERROR "Race id changed: ${race_name} is not ${race_id} (D20 masks)")
  endif()
endforeach()
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskUndead = 16;" "D20 Undead mask")
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskTroll = 128;" "D20 Troll mask")
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskNightElf = 8;" "D20 Night Elf mask")
require_text("${priest_strategy_h}" "constexpr uint32 RaceMaskHumanDwarf = 5;" "D20 Human+Dwarf mask")

region("${priest_strategy}" "bool ai::dietpriest::RaceMayLearn(PlayerbotAI* ai, uint32 raceMask)" "\n}" race_gate)
require_order("${race_gate}" "if (!sPlayerbotAIConfig.dietPriestRacials)\n        return true;" "bot->getRaceMask()" "D20 switch off registers every node before any race read")
require_text("${race_gate}" "return (bot->getRaceMask() & raceMask) != 0;" "D20 race mask test")

region("${priest_strategy}" "#ifdef MANGOSBOT_ZERO" "#ifdef MANGOSBOT_ONE" priest_zero)
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskHumanDwarf)" "critical health" "desperate prayer" "ACTION_EMERGENCY" "D20 desperate prayer")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskNightElf)" "target of attacker" "elune's grace" "ACTION_HIGH + 3" "D20 elune's grace")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskUndead)" "touch of weakness" "touch of weakness" "ACTION_NORMAL + 1" "D20 touch of weakness")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskTroll)" "shadowguard" "shadowguard" "ACTION_NORMAL + 1" "D20 shadowguard")
require_gated_node("${priest_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskNightElf)" "very often" "starshards" "ACTION_NORMAL + 1" "D20 starshards")
# The other nodes on the shared triggers stay ungated.
require_text("${priest_zero}" "\n    triggers.push_back(new TriggerNode(\n        \"critical health\",\n        NextAction::array(0, new NextAction(\"flash heal\", ACTION_CRITICAL_HEAL), NULL)));" "D20 flash heal stays")
require_text("${priest_zero}" "\n    triggers.push_back(new TriggerNode(\n        \"target of attacker\",\n        NextAction::array(0, new NextAction(\"power word: shield\", ACTION_HIGH), NULL)));" "D20 power word: shield stays")

region("${shadow_strategy}" "#ifdef MANGOSBOT_ZERO" "#ifdef MANGOSBOT_ONE" shadow_zero)
require_gated_node("${shadow_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskUndead)" "devouring plague" "devouring plague" "ACTION_NORMAL + 2" "D20 devouring plague")
require_gated_node("${shadow_zero}" "dietpriest::RaceMayLearn(ai, dietpriest::RaceMaskTroll)" "hex of weakness" "hex of weakness" "ACTION_NORMAL + 2" "D20 hex of weakness")

require_switch_first_trigger("${priest_triggers_h}" "DevouringPlagueTrigger" "DebuffTrigger" "devouring plague" "dietPriestRacials" "D20 devouring plague trigger")
require_switch_first_trigger("${priest_triggers_h}" "TouchOfWeaknessTrigger" "BuffTrigger" "touch of weakness" "dietPriestRacials" "D20 touch of weakness trigger")
require_switch_first_trigger("${priest_triggers_h}" "HexOfWeaknessTrigger" "DebuffTrigger" "hex of weakness" "dietPriestRacials" "D20 hex of weakness trigger")
require_switch_first_trigger("${priest_triggers_h}" "ShadowguardTrigger" "BuffTrigger" "shadowguard" "dietPriestRacials" "D20 shadowguard trigger")
forbid_text("${priest_triggers_h}" "BUFF_TRIGGER(TouchOfWeaknessTrigger" "D20 old macro trigger")
forbid_text("${priest_triggers_h}" "DEBUFF_TRIGGER(DevouringPlagueTrigger" "D20 old macro trigger")


# ---------------------------------------------------------------------------------------------------------
# D22 (AiPlayerbot.Diet.PriestSelfBuffKnown): inner fire / shadowform / inner focus wait for a known spell.
file(READ "${PB_SOURCE_DIR}/strategy/priest/PriestTriggers.cpp" priest_triggers_cpp)
region("${priest_triggers_cpp}" "bool InnerFireTrigger::IsActive()" "\n}" inner_fire)
require_order("${inner_fire}" "if (sPlayerbotAIConfig.dietPriestSelfBuffKnown && !AI_VALUE2(uint32, \"spell id\", spell))\n        return false;" "return SpellTrigger::IsActive() && !ai->HasAura(spell, target);" "D22 inner fire gate before the old check")
region("${priest_triggers_cpp}" "bool ShadowformTrigger::IsActive()" "\n}" shadowform)
require_order("${shadowform}" "if (sPlayerbotAIConfig.dietPriestSelfBuffKnown && !AI_VALUE2(uint32, \"spell id\", \"shadowform\"))\n        return false;" "return !ai->HasAura(\"shadowform\", bot);" "D22 shadowform gate before the old check")
region("${priest_triggers_cpp}" "bool InnerFocusTrigger::IsActive()" "\n}\n" inner_focus)
require_text("${inner_focus}" "if (sPlayerbotAIConfig.dietPriestSelfBuffKnown)\n    {\n        const uint32 spellId = AI_VALUE2(uint32, \"spell id\", spell);\n        if (!spellId || !bot->IsSpellReady(spellId))\n            return false;\n    }\n\n    return BuffTrigger::IsActive();" "D22 inner focus known+ready gate, then the old BuffTrigger check")
require_text("${priest_triggers_h}" "BUFF_TRIGGER_A(InnerFocusTrigger, \"inner focus\");" "D22 inner focus trigger declares IsActive")
forbid_text("${priest_triggers_h}" "BUFF_TRIGGER(InnerFocusTrigger" "D22 old inner focus macro")
# The nodes themselves stay as they were.
require_text("${priest_zero}" "\"inner fire\",\n        NextAction::array(0, new NextAction(\"inner fire\", ACTION_NORMAL + 1), NULL)));" "D22 inner fire node kept")
require_text("${priest_zero}" "\"inner focus\",\n        NextAction::array(0, new NextAction(\"inner focus\", ACTION_HIGH), NULL)));" "D22 boost inner focus node kept")
require_text("${shadow_zero}" "\"shadowform\",\n        NextAction::array(0, new NextAction(\"shadowform\", ACTION_MOVE), NULL)));" "D22 shadowform node kept")


# ---------------------------------------------------------------------------------------------------------
# D32 (AiPlayerbot.Diet.ShadowPriestDeadNodes): passive vampiric touch and the missing shadowfiend are not registered.
region("${shadow_strategy}" "void ShadowPriestStrategy::InitCombatTriggers(" "\n}\n" shadow_combat)
require_text("${shadow_combat}" "const bool dropDeadNodes = sPlayerbotAIConfig.dietShadowPriestDeadNodes;" "D32 switch read once")
require_text("${shadow_combat}" "    if (!dropDeadNodes)\n    {\n        triggers.push_back(new TriggerNode(\n            \"vampiric touch\",\n            NextAction::array(0, new NextAction(\"vampiric touch\", ACTION_NORMAL + 3), NULL)));\n    }" "D32 vampiric touch kept with the switch off")
require_text("${shadow_combat}" "    if (!dropDeadNodes)\n    {\n        triggers.push_back(new TriggerNode(\n            \"shadowfiend\",\n            NextAction::array(0, new NextAction(\"shadowfiend\", ACTION_HIGH + 5), NULL)));\n    }" "D32 shadowfiend kept with the switch off")
forbid_text("${shadow_combat}" "\n    triggers.push_back(new TriggerNode(\n        \"vampiric touch\"," "D32 ungated vampiric touch")
forbid_text("${shadow_combat}" "\n    triggers.push_back(new TriggerNode(\n        \"shadowfiend\"," "D32 ungated shadowfiend")
# Vampiric embrace (the castable half) stays ungated.
require_text("${shadow_combat}" "\n    triggers.push_back(new TriggerNode(\n        \"vampiric embrace\"," "D32 vampiric embrace stays")


# ---------------------------------------------------------------------------------------------------------
# D23 (AiPlayerbot.Diet.WarlockCcKnown): death coil snare and howl of terror only while the spell is known.
file(READ "${PB_SOURCE_DIR}/strategy/warlock/WarlockTriggers.h" warlock_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/warlock/WarlockTriggers.cpp" warlock_triggers_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/warlock/WarlockStrategy.cpp" warlock_strategy)
file(READ "${PB_SOURCE_DIR}/strategy/warlock/WarlockAiObjectContext.cpp" warlock_context)
region("${warlock_strategy}" "#ifdef MANGOSBOT_ZERO" "#ifdef MANGOSBOT_ONE" warlock_zero)
require_switch_first_trigger("${warlock_triggers_h}" "DeathCoilSnareTrigger" "SnareTargetTrigger" "death coil" "dietWarlockCcKnown" "D23 death coil snare trigger")
forbid_text("${warlock_triggers_h}" "SNARE_TRIGGER(DeathCoilSnareTrigger" "D23 old death coil snare macro")
region("${warlock_triggers_h}" "class HowlOfTerrorTrigger : public EnemyInRangeTrigger" "};" howl_trigger)
require_text("${howl_trigger}" "HowlOfTerrorTrigger(PlayerbotAI* ai) : EnemyInRangeTrigger(ai, \"howl of terror\", 10.0f) {}" "D23 howl trigger = the 10 yd check of enemy ten yards")
require_text("${howl_trigger}" "return AI_VALUE2(uint32, \"spell id\", \"howl of terror\") && EnemyInRangeTrigger::IsActive();" "D23 howl trigger known gate")
require_text("${warlock_context}" "creators[\"howl of terror\"] = [](PlayerbotAI* ai) { return new HowlOfTerrorTrigger(ai); };" "D23 howl trigger registered")
require_text("${warlock_zero}" "    triggers.push_back(new TriggerNode(\n        sPlayerbotAIConfig.dietWarlockCcKnown ? \"howl of terror\" : \"enemy ten yards\",\n        NextAction::array(0, new NextAction(\"howl of terror\", ACTION_INTERRUPT), NULL)));" "D23 howl node: old trigger with the switch off")
require_text("${warlock_zero}" "\"death coil on snare target\",\n        NextAction::array(0, new NextAction(\"death coil on snare target\", ACTION_INTERRUPT + 3), NULL)));" "D23 death coil snare node kept")
require_text("${trigger_context}" "creators[\"enemy ten yards\"] = [](PlayerbotAI* ai) { return new EnemyInRangeTrigger(ai, \"enemy ten yards\", 10.0f); };" "D23 generic enemy ten yards = EnemyInRangeTrigger 10 yd (HowlOfTerrorTrigger mirrors it)")


# ---------------------------------------------------------------------------------------------------------
# D24 (AiPlayerbot.Diet.WarlockTalentKnown): siphon life, conflagrate, shadowburn, dark pact only when known.
file(READ "${PB_SOURCE_DIR}/strategy/warlock/AfflictionWarlockStrategy.cpp" affliction_strategy)
region("${affliction_strategy}" "#ifdef MANGOSBOT_ZERO" "#ifdef MANGOSBOT_ONE" affliction_zero)
require_switch_first_trigger("${warlock_triggers_h}" "SiphonLifeTrigger" "DebuffTrigger" "siphon life" "dietWarlockTalentKnown" "D24 siphon life trigger")
forbid_text("${warlock_triggers_h}" "DEBUFF_TRIGGER(SiphonLifeTrigger" "D24 old siphon life macro")
require_switch_first_trigger("${warlock_triggers_h}" "SiphonLifeOnAttackerTrigger" "DebuffOnAttackerTrigger" "siphon life" "dietWarlockTalentKnown" "D24 siphon life on attacker trigger")
region("${warlock_triggers_cpp}" "bool ConflagrateTrigger::IsActive()" "\n}" conflagrate)
require_order("${conflagrate}" "if (sPlayerbotAIConfig.dietWarlockTalentKnown && !AI_VALUE2(uint32, \"spell id\", \"conflagrate\"))\n        return false;" "Aura* aura = ai->GetAura(\"immolate\", target, true);" "D24 conflagrate gate before the old immolate check")
region("${warlock_triggers_h}" "class ShadowburnTrigger : public TargetCriticalHealthTrigger" "};" shadowburn_trigger)
require_text("${shadowburn_trigger}" "return AI_VALUE2(uint32, \"spell id\", \"shadowburn\") && TargetCriticalHealthTrigger::IsActive();" "D24 shadowburn = target critical health + known")
region("${warlock_triggers_h}" "class ShadowburnPvpTrigger : public TimeTrigger" "};" shadowburn_pvp_trigger)
require_text("${shadowburn_pvp_trigger}" "ShadowburnPvpTrigger(PlayerbotAI* ai) : TimeTrigger(ai, \"shadowburn pvp\", 5) {}" "D24 pvp shadowburn keeps the 5 s very often interval")
require_text("${shadowburn_pvp_trigger}" "return AI_VALUE2(uint32, \"spell id\", \"shadowburn\") != 0;" "D24 pvp shadowburn known")
require_text("${trigger_context}" "creators[\"very often\"] = [](PlayerbotAI* ai) { return new TimeTrigger(ai, \"very often\", 5); };" "D24 generic very often = TimeTrigger 5 s (ShadowburnPvpTrigger mirrors it)")
require_text("${trigger_context}" "creators[\"target critical health\"] = [](PlayerbotAI* ai) { return new TargetCriticalHealthTrigger(ai); };" "D24 generic target critical health")
require_text("${trigger_context}" "creators[\"low mana\"] = [](PlayerbotAI* ai) { return new LowManaTrigger(ai); };" "D24 generic low mana")
region("${warlock_triggers_h}" "class DarkPactTrigger : public LowManaTrigger" "};" dark_pact_trigger)
require_text("${dark_pact_trigger}" "return AI_VALUE2(uint32, \"spell id\", \"dark pact\") && LowManaTrigger::IsActive();" "D24 dark pact = low mana + known")
foreach(reg "shadowburn|ShadowburnTrigger" "shadowburn pvp|ShadowburnPvpTrigger" "dark pact|DarkPactTrigger")
  string(REPLACE "|" ";" parts "${reg}")
  list(GET parts 0 reg_name)
  list(GET parts 1 reg_class)
  require_text("${warlock_context}" "creators[\"${reg_name}\"] = [](PlayerbotAI* ai) { return new ${reg_class}(ai); };" "D24 ${reg_name} trigger registered")
endforeach()
require_text("${warlock_zero}" "        sPlayerbotAIConfig.dietWarlockTalentKnown ? \"shadowburn\" : \"target critical health\",  // twow-repo#568 (D24)\n        NextAction::array(0, new NextAction(\"shadowburn\", ACTION_HIGH), NULL)));" "D24 shadowburn node: old trigger with the switch off")
require_text("${warlock_zero}" "        sPlayerbotAIConfig.dietWarlockTalentKnown ? \"shadowburn pvp\" : \"very often\",  // twow-repo#568 (D24)\n        NextAction::array(0, new NextAction(\"shadowburn\", ACTION_HIGH), NULL)));" "D24 pvp shadowburn node: old trigger with the switch off")
require_text("${affliction_zero}" "        sPlayerbotAIConfig.dietWarlockTalentKnown ? \"dark pact\" : \"low mana\",  // twow-repo#568 (D24)\n        NextAction::array(0, new NextAction(\"dark pact\", ACTION_HIGH + 2), NULL)));" "D24 dark pact node: old trigger with the switch off")
forbid_text("${warlock_zero}" "\"target critical health\",\n        NextAction::array(0, new NextAction(\"shadowburn\"" "D24 ungated shadowburn")
forbid_text("${affliction_zero}" "\"low mana\",\n        NextAction::array(0, new NextAction(\"dark pact\"" "D24 ungated dark pact")


# ---------------------------------------------------------------------------------------------------------
# D25 (AiPlayerbot.Diet.WarlockAmplifyCurse): the deprecated amplify curse node is not registered.
region("${warlock_zero}" "void WarlockBoostStrategy::InitCombatTriggers(" "\n}\n" warlock_boost)
require_gated_node("${warlock_boost}" "!sPlayerbotAIConfig.dietWarlockAmplifyCurse" "amplify curse" "amplify curse" "ACTION_HIGH" "D25 amplify curse")
require_text("${warlock_boost}" "\n    triggers.push_back(new TriggerNode(\n        \"inferno\"," "D25 inferno stays")


# ---------------------------------------------------------------------------------------------------------
# D33 (AiPlayerbot.Diet.MageArmorKnown): a mage without mage armor asks for the best armor it knows.
file(READ "${PB_SOURCE_DIR}/strategy/mage/MageTriggers.cpp" mage_triggers_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/mage/MageStrategy.cpp" mage_strategy)
region("${mage_triggers_cpp}" "bool MageArmorTrigger::IsActive()" "\n}" mage_armor)
require_text("${mage_armor}" "    if (sPlayerbotAIConfig.dietMageArmorKnown && !AI_VALUE2(uint32, \"spell id\", \"mage armor\"))\n    {\n        if (AI_VALUE2(uint32, \"spell id\", \"ice armor\"))\n            return !ai->HasAura(\"ice armor\", target);\n\n        return !ai->HasAura(\"frost armor\", target);\n    }\n\n    return !ai->HasAura(\"mage armor\", target);" "D33 best-known armor check; switch off = old mage armor check")
# The alternative chain that casts the armor stays untouched.
require_text("${mage_strategy}" "ACTION_NODE_A(mage_armor, \"mage armor\", \"ice armor\");" "D33 mage armor -> ice armor chain kept")
require_text("${mage_strategy}" "ACTION_NODE_A(ice_armor, \"ice armor\", \"frost armor\");" "D33 ice armor -> frost armor chain kept")

message(STATUS "DIET_G2_SOURCE_CONTRACT=PASS")
