cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g1 (warrior + rogue), every fix behind its own AiPlayerbot.Diet.* switch,
# default 0 = today's trigger lists. Strategies build their lists at engine init, so a switch takes effect at the
# next engine init. Each block checks: default off (header, reader, dist), the gate, and the kept off path.
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

# Switch: member defaults to false, reader defaults to false, dist documents 0.
function(require_switch member key)
  require_text("${config_h}" "    bool ${member} = false;  // twow-repo#568 (diet " "${member} member default false")
  require_text("${config_cpp}" "    ${member} = config.GetBoolDefault(\"AiPlayerbot.${key}\", false);" "${key} read with default off")
  require_text("${config_dist}" "\nAiPlayerbot.${key} = 0\n" "${key} = 0 in the dist config")
endfunction()

# Only the vanilla (MANGOSBOT_ZERO) section of a strategy file is built here.
function(read_vanilla path out)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  region("${text}" "#ifdef MANGOSBOT_ZERO" "#endif" body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# D03: combat rogue boost cooldowns registered in the combat engine (on) or, as today, the non-combat engine (off).
require_switch(dietRogueCombatBoostInCombat Diet.RogueCombatBoostInCombat)
read_vanilla(strategy/rogue/CombatRogueStrategy.cpp combat_rogue)
region("${combat_rogue}" "void CombatRogueBoostStrategy::InitCombatTriggers(" "void CombatRogueBoostStrategy::InitNonCombatTriggers(" d03_combat)
require_order("${d03_combat}" "if (sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"adrenaline rush\", ACTION_HIGH + 5)" "D03 adrenaline rush in combat only behind the switch")
require_order("${d03_combat}" "if (sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"blade flurry\", ACTION_HIGH + 4)" "D03 blade flurry in combat only behind the switch")
forbid_text("${d03_combat}" "!sPlayerbotAIConfig.dietRogueCombatBoostInCombat" "D03 inverted gate in the combat list")
region("${combat_rogue}" "void CombatRogueBoostStrategy::InitNonCombatTriggers(" "void CombatRogueBoostPveStrategy::InitCombatTriggers(" d03_nc)
require_order("${d03_nc}" "if (!sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"adrenaline rush\", ACTION_HIGH + 5)" "D03 off path keeps adrenaline rush in the non-combat list")
require_order("${d03_nc}" "if (!sPlayerbotAIConfig.dietRogueCombatBoostInCombat)" "NextAction(\"blade flurry\", ACTION_HIGH + 4)" "D03 off path keeps blade flurry in the non-combat list")

# D15: assassination builders without the non-existent Mutilate (on); the mutilate nodes stay on the off path.
require_switch(dietRogueNoMutilate Diet.RogueNoMutilate)
read_vanilla(strategy/rogue/AssassinationRogueStrategy.cpp assassination)
region("${assassination}" "void AssassinationRogueStrategy::InitCombatTriggers(" "void AssassinationRogueStrategy::InitNonCombatTriggers(" d15)
region("${d15}" "if (sPlayerbotAIConfig.dietRogueNoMutilate)" "else" d15_on)
require_text("${d15_on}" "\"behind target\",
            NextAction::array(0, new NextAction(\"backstab\", ACTION_NORMAL + 1), NULL)" "D15 on: backstab behind the target")
require_text("${d15_on}" "\"not behind target\",
            NextAction::array(0, new NextAction(\"sinister strike\", ACTION_NORMAL), NULL)" "D15 on: sinister strike in front")
forbid_text("${d15_on}" "mutilate" "D15 on: no mutilate node")
region("${d15}" "    else\n" "\n    }\n" d15_off)
require_text("${d15_off}" "NextAction(\"mutilate back\", ACTION_NORMAL + 1)" "D15 off path keeps mutilate back")
require_text("${d15_off}" "NextAction(\"mutilate front\", ACTION_NORMAL)" "D15 off path keeps mutilate front")

# D16: protection without the non-existent Heroic Throw (on); the heroic throw nodes stay on the off path.
require_switch(dietWarriorNoHeroicThrow Diet.WarriorNoHeroicThrow)
read_vanilla(strategy/warrior/ProtectionWarriorStrategy.cpp prot)
region("${prot}" "void ProtectionWarriorStrategy::InitCombatTriggers(" "void ProtectionWarriorStrategy::InitNonCombatTriggers(" prot_combat)
require_text("${prot_combat}" "const bool noHeroicThrow = sPlayerbotAIConfig.dietWarriorNoHeroicThrow;" "D16 switch read")
region("${prot_combat}" "    if (noHeroicThrow)\n" "    else\n" d16_on_move)
require_text("${d16_on_move}" "\"enemy out of melee\",
            NextAction::array(0, new NextAction(\"charge\", ACTION_MOVE + 7), NULL)" "D16 on: charge only when out of melee")
forbid_text("${d16_on_move}" "heroic throw" "D16 on: no heroic throw before charge")
region("${prot_combat}" "\"intercept and rage\"" "\"demoralizing shout\"" d16_taunts)
region("${d16_taunts}" "    if (noHeroicThrow)\n" "    else\n" d16_on_taunt)
require_text("${d16_on_taunt}" "\"lose aggro\",
            NextAction::array(0, new NextAction(\"taunt\", ACTION_MOVE + 4), NULL)" "D16 on: taunt on lose aggro")
require_text("${d16_on_taunt}" "\"taunt on snare target\",
            NextAction::array(0, new NextAction(\"taunt on snare target\", ACTION_MOVE), NULL)" "D16 on: taunt on snare target")
forbid_text("${d16_on_taunt}" "heroic throw" "D16 on: no heroic throw taunt")
region("${d16_taunts}" "    else\n" "\n    }\n" d16_off_taunt)
require_text("${d16_off_taunt}" "NextAction(\"heroic throw taunt\", ACTION_MOVE + 4)" "D16 off path keeps heroic throw taunt")
require_text("${d16_off_taunt}" "NextAction(\"heroic throw on snare target\", ACTION_MOVE)" "D16 off path keeps heroic throw on snare target")
require_text("${prot_combat}" "NextAction(\"heroic throw\", ACTION_MOVE + 8), new NextAction(\"charge\", ACTION_MOVE + 7)" "D16 off path keeps heroic throw before charge")

# D17: arms snare answered by hamstring directly (on); the piercing howl -> mocking blow chain stays on the off path.
require_switch(dietArmsSnareHamstring Diet.ArmsSnareHamstring)
read_vanilla(strategy/warrior/ArmsWarriorStrategy.cpp arms)
region("${arms}" "void ArmsWarriorCcStrategy::InitCombatTriggers(" "void ArmsWarriorCcStrategy::InitNonCombatTriggers(" d17)
region("${d17}" "if (sPlayerbotAIConfig.dietArmsSnareHamstring)" "else" d17_on)
require_text("${d17_on}" "\"hamstring\",
            NextAction::array(0, new NextAction(\"hamstring\", ACTION_HIGH), NULL)" "D17 on: hamstring directly")
forbid_text("${d17_on}" "piercing howl" "D17 on: no piercing howl")
region("${d17}" "    else\n" "\n    }\n" d17_off)
require_text("${d17_off}" "NextAction(\"piercing howl\", ACTION_HIGH)" "D17 off path keeps piercing howl")
file(READ "${PB_SOURCE_DIR}/strategy/warrior/ArmsWarriorStrategy.cpp" arms_full)
require_text("${arms_full}" "ACTION_NODE_A(piercing_howl, \"piercing howl\", \"mocking blow\");" "D17 off path keeps the arms piercing howl node")

# D36: protection sunder armor without the unlearnable Devastate (on); devastate stays on the off path.
require_switch(dietWarriorNoDevastate Diet.WarriorNoDevastate)
region("${prot_combat}" "if (sPlayerbotAIConfig.dietWarriorNoDevastate)" "else" d36_on)
require_text("${d36_on}" "\"sunder armor\",
            NextAction::array(0, new NextAction(\"sunder armor\", ACTION_HIGH + 2), NULL)" "D36 on: sunder armor directly")
forbid_text("${d36_on}" "devastate" "D36 on: no devastate")
region("${prot_combat}" "if (sPlayerbotAIConfig.dietWarriorNoDevastate)" "\"revenge\"" d36_all)
region("${d36_all}" "    else\n" "\n    }\n" d36_off)
require_text("${d36_off}" "NextAction(\"devastate\", ACTION_HIGH + 2)" "D36 off path keeps devastate")

# D37: disarm trigger gated on a known, ready Disarm and Defensive Stance (on); the debuff check stays the off path.
require_switch(dietWarriorDisarmGate Diet.WarriorDisarmGate)
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorTriggers.h" warrior_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorTriggers.cpp" warrior_triggers_cpp)
require_text("${warrior_triggers_h}" "DEBUFF_TRIGGER_A(DisarmDebuffTrigger, \"disarm\");" "D37 disarm trigger with its own IsActive")
forbid_text("${warrior_triggers_h}" "DEBUFF_TRIGGER(DisarmDebuffTrigger" "D37 second disarm trigger definition")
require_text("${warrior_triggers_cpp}" "#include \"playerbot/PlayerbotAIConfig.h\"" "D37 config include")
region("${warrior_triggers_cpp}" "bool DisarmDebuffTrigger::IsActive()" "\n}\n" d37)
region("${d37}" "if (sPlayerbotAIConfig.dietWarriorDisarmGate)" "\n    }\n" d37_on)
require_text("${d37_on}" "const uint32 spellId = AI_VALUE2(uint32, \"spell id\", \"disarm\");" "D37 known check")
require_text("${d37_on}" "if (!spellId || !bot->IsSpellReady(spellId) || !ai->HasAura(\"defensive stance\", bot))
            return false;" "D37 known / ready / stance gate")
require_order("${d37}" "if (sPlayerbotAIConfig.dietWarriorDisarmGate)" "return DebuffTrigger::IsActive();" "D37 gate before the old debuff check")
string(REGEX MATCHALL "return " d37_returns "${d37}")
list(LENGTH d37_returns d37_return_count)
if(NOT d37_return_count EQUAL 2)
  message(FATAL_ERROR "D37: expected exactly the gate return and the old debuff return, got ${d37_return_count}")
endif()

# D38: berserker stance trigger needs the spell; the Defensive Tactics swap also the talent and a shield (on).
require_switch(dietWarriorBerserkerStanceGate Diet.WarriorBerserkerStanceGate)
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorTriggers.h" warrior_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorTriggers.cpp" warrior_triggers_cpp)
require_text("${warrior_triggers_h}" "BUFF_TRIGGER_A(BerserkerStanceTrigger, \"berserker stance\");" "D38 berserker stance trigger with its own IsActive")
forbid_text("${warrior_triggers_h}" "BUFF_TRIGGER(BerserkerStanceTrigger" "D38 second berserker stance trigger definition")
region("${warrior_triggers_h}" "class DefensiveTacticsBerserkerStanceTrigger" "};" d38_dt_h)
require_text("${d38_dt_h}" "virtual bool IsActive() override;" "D38 DT trigger body moved to the .cpp")
forbid_text("${d38_dt_h}" "AI_VALUE2" "D38 second DT trigger body in the header")
region("${warrior_triggers_cpp}" "bool BerserkerStanceTrigger::IsActive()" "\n}\n" d38_bs)
require_text("${d38_bs}" "if (sPlayerbotAIConfig.dietWarriorBerserkerStanceGate && !AI_VALUE2(uint32, \"spell id\", \"berserker stance\"))
        return false;" "D38 spell known gate")
require_order("${d38_bs}" "sPlayerbotAIConfig.dietWarriorBerserkerStanceGate" "return BuffTrigger::IsActive();" "D38 off path: the plain buff check")
region("${warrior_triggers_cpp}" "bool DefensiveTacticsBerserkerStanceTrigger::IsActive()" "\n}\n" d38_dt)
# Off path in the old order: buff check, combat, HP > 70.
require_order("${d38_dt}" "if (!BerserkerStanceTrigger::IsActive())
        return false;" "if (!sServerFacade.IsInCombat(bot))
        return false;" "D38 DT off path order 1")
require_order("${d38_dt}" "if (!sServerFacade.IsInCombat(bot))" "return AI_VALUE2(uint8, \"health\", \"self target\") > 70;" "D38 DT off path order 2")
region("${d38_dt}" "if (sPlayerbotAIConfig.dietWarriorBerserkerStanceGate)" "\n    }\n" d38_dt_on)
require_text("${d38_dt_on}" "if (!bot->HasSpell(51606) && !bot->HasSpell(51607) && !bot->HasSpell(51608))
            return false;" "D38 DT talent gate")
require_text("${d38_dt_on}" "Item* offhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);" "D38 offhand read")
require_text("${d38_dt_on}" "offhand->GetProto()->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD)
            return false;" "D38 shield gate")

message(STATUS "DIET_G1_WARRIOR_ROGUE_SOURCE_CONTRACT=PASS")
