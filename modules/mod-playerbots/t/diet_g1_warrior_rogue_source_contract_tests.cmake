cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g1 (warrior + rogue). The nine AiPlayerbot.Diet.* switches were accepted (profile 1)
# and retired (switch audit A2 #1, owner 04.11.2026 "alter code löschen"): the fixed trigger lists are the only path, the
# old branches with the dead TBC/WotLK nodes are gone. Each block checks the kept fix and that the old path cannot return.
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

# Retired switch: no member, no reader, no dist key, no use anywhere in the warrior / rogue strategies.
set(strategy_files
  strategy/rogue/CombatRogueStrategy.cpp strategy/rogue/AssassinationRogueStrategy.cpp
  strategy/warrior/ProtectionWarriorStrategy.cpp strategy/warrior/ArmsWarriorStrategy.cpp
  strategy/warrior/FuryWarriorStrategy.cpp strategy/warrior/WarriorStrategy.cpp
  strategy/warrior/WarriorTriggers.cpp strategy/warrior/WarriorTriggers.h)
set(all_strategies "")
foreach(f ${strategy_files})
  file(READ "${PB_SOURCE_DIR}/${f}" t)
  string(APPEND all_strategies "${t}")
endforeach()
function(require_retired member key)
  forbid_text("${config_h}" "${member}" "${key} member (retired)")
  forbid_text("${config_cpp}" "${member}" "${key} reader (retired)")
  forbid_text("${config_dist}" "AiPlayerbot.${key}" "${key} in the dist config (retired)")
  forbid_text("${all_strategies}" "${member}" "${key} use in the strategies (retired)")
endfunction()
foreach(pair
    "dietRogueCombatBoostInCombat;Diet.RogueCombatBoostInCombat" "dietRogueNoMutilate;Diet.RogueNoMutilate"
    "dietWarriorNoHeroicThrow;Diet.WarriorNoHeroicThrow" "dietArmsSnareHamstring;Diet.ArmsSnareHamstring"
    "dietWarriorNoDevastate;Diet.WarriorNoDevastate" "dietWarriorDisarmGate;Diet.WarriorDisarmGate"
    "dietWarriorBerserkerStanceGate;Diet.WarriorBerserkerStanceGate"
    "dietWarriorNoInterceptAndRage;Diet.WarriorNoInterceptAndRage" "dietWarriorDedupBloodrage;Diet.WarriorDedupBloodrage")
  list(GET pair 0 member)
  list(GET pair 1 key)
  require_retired(${member} ${key})
endforeach()

# Only the vanilla (MANGOSBOT_ZERO) section of a strategy file is built here.
function(read_vanilla path out)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  region("${text}" "#ifdef MANGOSBOT_ZERO" "#endif" body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# D03: the combat rogue boost cooldowns are registered in the combat engine only.
read_vanilla(strategy/rogue/CombatRogueStrategy.cpp combat_rogue)
region("${combat_rogue}" "void CombatRogueBoostStrategy::InitCombatTriggers(" "void CombatRogueBoostStrategy::InitNonCombatTriggers(" d03_combat)
require_text("${d03_combat}" "NextAction(\"adrenaline rush\", ACTION_HIGH + 5)" "D03 adrenaline rush in the combat engine")
require_text("${d03_combat}" "NextAction(\"blade flurry\", ACTION_HIGH + 4)" "D03 blade flurry in the combat engine")
region("${combat_rogue}" "void CombatRogueBoostStrategy::InitNonCombatTriggers(" "void CombatRogueBoostPveStrategy::InitCombatTriggers(" d03_nc)
forbid_text("${d03_nc}" "NextAction(\"adrenaline rush\"" "D03 adrenaline rush in the non-combat engine (never cast there)")
forbid_text("${d03_nc}" "NextAction(\"blade flurry\"" "D03 blade flurry in the non-combat engine (never cast there)")

# D15: assassination builders are backstab behind / sinister strike in front; Mutilate does not exist in Turtle 1.18.
read_vanilla(strategy/rogue/AssassinationRogueStrategy.cpp assassination)
region("${assassination}" "void AssassinationRogueStrategy::InitCombatTriggers(" "void AssassinationRogueStrategy::InitNonCombatTriggers(" d15)
require_text("${d15}" "\"behind target\",
        NextAction::array(0, new NextAction(\"backstab\", ACTION_NORMAL + 1), NULL)));" "D15 backstab behind the target")
require_text("${d15}" "\"not behind target\",
        NextAction::array(0, new NextAction(\"sinister strike\", ACTION_NORMAL), NULL)));" "D15 sinister strike in front")
forbid_text("${d15}" "NextAction(\"mutilate" "D15 a mutilate node")

# Protection combat list: D16 (no Heroic Throw), D36 (no Devastate), D39 (no 'intercept and rage'), D40 (no duplicates).
read_vanilla(strategy/warrior/ProtectionWarriorStrategy.cpp prot)
region("${prot}" "void ProtectionWarriorStrategy::InitCombatTriggers(" "void ProtectionWarriorStrategy::InitNonCombatTriggers(" prot_combat)
require_text("${prot_combat}" "\"enemy out of melee\",
        NextAction::array(0, new NextAction(\"charge\", ACTION_MOVE + 7), NULL)));" "D16 charge without heroic throw")
require_text("${prot_combat}" "\"lose aggro\",
        NextAction::array(0, new NextAction(\"taunt\", ACTION_MOVE + 4), NULL)));" "D16 taunt on lose aggro")
require_text("${prot_combat}" "\"taunt on snare target\",
        NextAction::array(0, new NextAction(\"taunt on snare target\", ACTION_MOVE), NULL)));" "D16 taunt on snare target")
forbid_text("${prot_combat}" "NextAction(\"heroic throw" "D16 a heroic throw node")
require_text("${prot_combat}" "\"sunder armor\",
        NextAction::array(0, new NextAction(\"sunder armor\", ACTION_HIGH + 2), NULL)));" "D36 sunder armor directly")
forbid_text("${prot_combat}" "NextAction(\"devastate\"" "D36 a devastate node")
forbid_text("${prot_combat}" "\"intercept and rage\"" "D39 an 'intercept and rage' node")
forbid_text("${prot_combat}" "NextAction(\"concussion blow\", ACTION_HIGH + 4)" "D40 duplicate concussion blow")
forbid_text("${prot_combat}" "NextAction(\"bloodrage\", ACTION_NORMAL + 5)" "D40 duplicate bloodrage")
# The kept registrations: cc concussion blow at INTERRUPT and buff bloodrage at HIGH.
region("${prot}" "void ProtectionWarriorCcStrategy::InitCombatTriggers(" "void ProtectionWarriorCcStrategy::InitNonCombatTriggers(" d40_cc)
require_text("${d40_cc}" "NextAction(\"concussion blow\", ACTION_INTERRUPT)" "D40 cc concussion blow kept")
read_vanilla(strategy/warrior/WarriorStrategy.cpp warrior_base)
region("${warrior_base}" "void WarriorBuffStrategy::InitCombatTriggers(" "void WarriorBuffStrategy::InitNonCombatTriggers(" d40_buff)
require_text("${d40_buff}" "NextAction(\"bloodrage\", ACTION_HIGH)" "D40 buff bloodrage kept")

# D17: the arms snare trigger pushes Hamstring directly (Piercing Howl is a Fury talent).
read_vanilla(strategy/warrior/ArmsWarriorStrategy.cpp arms)
region("${arms}" "void ArmsWarriorCcStrategy::InitCombatTriggers(" "void ArmsWarriorCcStrategy::InitNonCombatTriggers(" d17)
require_text("${d17}" "\"hamstring\",
        NextAction::array(0, new NextAction(\"hamstring\", ACTION_HIGH), NULL)));" "D17 hamstring directly")
forbid_text("${d17}" "NextAction(\"piercing howl\"" "D17 the piercing howl node")

# Warrior triggers: D37 (Disarm known, ready, Defensive Stance), D38 (Berserker Stance known; no Defensive Tactics swap),
# D40 (Bloodrage known and ready).
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorTriggers.h" warrior_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorTriggers.cpp" warrior_triggers_cpp)
region("${warrior_triggers_cpp}" "bool DisarmDebuffTrigger::IsActive()" "\n}\n" d37)
require_order("${d37}" "    const uint32 spellId = AI_VALUE2(uint32, \"spell id\", \"disarm\");
    if (!spellId || !bot->IsSpellReady(spellId) || !ai->HasAura(\"defensive stance\", bot))
        return false;" "return DebuffTrigger::IsActive();" "D37 gate before the debuff check")
region("${warrior_triggers_cpp}" "bool BloodrageBuffTrigger::IsActive()" "\n}\n" d40_trigger)
require_order("${d40_trigger}" "    const uint32 spellId = AI_VALUE2(uint32, \"spell id\", \"bloodrage\");
    if (!spellId || !bot->IsSpellReady(spellId))
        return false;" "return AI_VALUE2(uint8, \"health\", \"self target\")" "D40 cooldown gate before the health / rage check")
require_text("${warrior_triggers_h}" "BUFF_TRIGGER_A(BerserkerStanceTrigger, \"berserker stance\");" "D38 berserker stance trigger with its own IsActive")
region("${warrior_triggers_cpp}" "bool BerserkerStanceTrigger::IsActive()" "\n}\n" d38_bs)
require_order("${d38_bs}" "    if (!AI_VALUE2(uint32, \"spell id\", \"berserker stance\"))
        return false;" "return BuffTrigger::IsActive();" "D38 spell known before the buff check")
region("${warrior_triggers_cpp}" "bool DefensiveTacticsBerserkerStanceTrigger::IsActive()" "\n}\n" d38_dt)
require_text("${d38_dt}" "    return false;" "D38 no Defensive Tactics swap (tanks stay in Defensive Stance)")
forbid_text("${d38_dt}" "AI_VALUE2(uint8, \"health\"" "D38 the old HP > 70 swap")
# Arms stays in Battle Stance: no berserker stance node in the arms buff strategy; fury keeps its node.
region("${arms}" "void ArmsWarriorBuffStrategy::InitCombatTriggers(" "void ArmsWarriorBuffStrategy::InitNonCombatTriggers(" d38_arms)
forbid_text("${d38_arms}" "NextAction(\"berserker stance\"" "D38 arms berserker stance node")
read_vanilla(strategy/warrior/FuryWarriorStrategy.cpp fury)
region("${fury}" "void FuryWarriorBuffStrategy::InitCombatTriggers(" "void FuryWarriorBuffStrategy::InitNonCombatTriggers(" d38_fury)
require_text("${d38_fury}" "    triggers.push_back(new TriggerNode(
        \"berserker stance\",
        NextAction::array(0, new NextAction(\"berserker stance\", ACTION_MOVE), NULL)));" "D38 fury berserker stance node")
# Berserker Rage: no berserker stance prerequisite.
file(READ "${PB_SOURCE_DIR}/strategy/warrior/WarriorStrategy.cpp" warrior_strategy_full)
region("${warrior_strategy_full}" "static ActionNode* berserker_rage(PlayerbotAI* ai)" "\n    }\n" d38_br)
require_text("${d38_br}" "            /*P*/ NULL," "D38 berserker rage without a prerequisite")
forbid_text("${d38_br}" "NextAction(\"berserker stance\"" "D38 berserker stance prerequisite of berserker rage")

message(STATUS "DIET_G1_WARRIOR_ROGUE_SOURCE_CONTRACT=PASS")
