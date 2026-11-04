cmake_policy(VERSION 3.16)
# twow-repo#568 strategy diet, group g3 (shaman, paladin, druid). The thirteen AiPlayerbot.Diet.* switches were accepted
# (profile 1) and retired (switch audit A2 #1, owner 04.11.2026 "alter code löschen"): the fixed vanilla path is the only
# path. Each block checks the kept fix and that the dead TBC/WotLK node cannot return to the vanilla section.
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

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR next "${at} + ${len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "${description}: expected ${expected}, found ${n}")
  endif()
endfunction()

# The vanilla section of a strategy file ("#ifdef MANGOSBOT_ZERO // Vanilla" up to its #endif).
function(read_vanilla path out)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  region("${text}" "#ifdef MANGOSBOT_ZERO // Vanilla" "#endif" body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
file(GLOB_RECURSE class_files "${PB_SOURCE_DIR}/strategy/shaman/*" "${PB_SOURCE_DIR}/strategy/paladin/*" "${PB_SOURCE_DIR}/strategy/druid/*")
set(class_sources "")
foreach(f ${class_files})
  file(READ "${f}" t)
  string(APPEND class_sources "${t}")
endforeach()

# Retired switch: no member, no reader, no dist key, no use in the shaman / paladin / druid strategies.
foreach(key
    LevelingDruidWrath ShockCooldownGate FireNovaTotem NoWrathOfAirTotem ShamanVanillaNames InnervateFix RetRepentanceKey
    ExorcismCreatureType ProtSealDirect FeralNoMangle FeralNoCatSwipe DruidCooldownGates LevelingDruidKnownSpells)
  set(member "diet${key}")
  forbid_text("${config_h}" "${member}" "${key} member (retired)")
  forbid_text("${config_cpp}" "${member}" "${key} reader (retired)")
  forbid_text("${config_dist}" "AiPlayerbot.Diet.${key}" "${key} in the dist config (retired)")
  forbid_text("${class_sources}" "${member}" "${key} use in the strategies (retired)")
endforeach()

# D04: the leveling druid's Wrath node uses the trigger name that has a creator.
file(READ "${PB_SOURCE_DIR}/strategy/druid/LevelingDruidStrategy.cpp" leveling)
require_text("${leveling}" "        \"enemy out of melee\",\n        NextAction::array(0, new NextAction(\"wrath\", ACTION_NORMAL), NULL)));" "D04 wrath on 'enemy out of melee'")
forbid_text("${leveling}" "\"enemy out of melee range\"" "D04 the trigger name without a creator")

# D26: the shock trigger stays quiet while Earth Shock or the shared shock category cools down.
file(READ "${PB_SOURCE_DIR}/strategy/shaman/ShamanTriggers.cpp" shaman_triggers_cpp)
region("${shaman_triggers_cpp}" "bool ShockTrigger::IsActive()" "\n}\n" shock)
require_order("${shock}" "    uint32 const shockId = AI_VALUE2(uint32, \"spell id\", \"earth shock\");
    if (shockId)
    {
        SpellEntry const* shockInfo = sServerFacade.LookupSpellInfo(shockId);
        if (shockInfo && (bot->HasSpellCooldown(shockId) || bot->HasSpellCategoryCooldown(shockInfo->Category)))
            return false;
    }" "return SpellTrigger::IsActive()" "D26 cooldown gate before the old check")

# D27: Fire Nova Totem (the castable spell) instead of "fire nova"; a placed one holds the fire slot; no Magma continuer.
read_vanilla(strategy/shaman/ElementalShamanStrategy.cpp elemental)
read_vanilla(strategy/shaman/EnhancementShamanStrategy.cpp enhancement)
read_vanilla(strategy/shaman/RestorationShamanStrategy.cpp restoration)
foreach(v elemental enhancement)
  require_text("${${v}}" "NextAction(\"fire nova totem\", ACTION_HIGH)" "D27 fire nova totem node (${v})")
  forbid_text("${${v}}" "NextAction(\"fire nova\"" "D27 the 'fire nova' node (${v})")
endforeach()
file(READ "${PB_SOURCE_DIR}/strategy/shaman/EnhancementShamanStrategy.cpp" enhancement_full)
file(READ "${PB_SOURCE_DIR}/strategy/shaman/RestorationShamanStrategy.cpp" restoration_full)
foreach(v enhancement_full restoration_full)
  region("${${v}}" "static bool DietDropMagmaContinuer()" "\n    }\n" magma_gate)
  require_text("${magma_gate}" "#ifdef MANGOSBOT_ZERO\n        return true;\n#else\n        return false;\n#endif" "D27 vanilla-only continuer drop (${v})")
endforeach()
file(READ "${PB_SOURCE_DIR}/strategy/shaman/ShamanTriggers.h" shaman_triggers_h)
count_text("${shaman_triggers_h}" "!AI_VALUE2(bool, \"has totem\", \"fire nova totem\");" 3 "D27 a placed fire nova totem holds the fire slot (both fire totem triggers + the fire nova totem trigger)")
file(READ "${PB_SOURCE_DIR}/strategy/shaman/ShamanAiObjectContext.cpp" shaman_context)
require_text("${shaman_context}" "new ShamanManualTotemStrategy(ai, \"totem fire nova\", \"fire totem\", \"fire nova totem\")" "D27 manual fire nova totem strategy")

# D28: no Wrath of Air Totem node in vanilla; the Windfury alternative drop is vanilla only.
foreach(v elemental restoration)
  forbid_text("${${v}}" "NextAction(\"wrath of air totem\", ACTION_HIGH)" "D28 the wrath of air totem node (${v})")
endforeach()
file(READ "${PB_SOURCE_DIR}/strategy/shaman/ElementalShamanStrategy.cpp" elemental_full)
region("${elemental_full}" "static bool DietNoWrathOfAirTotem()" "\n    }\n" woa_gate)
require_text("${woa_gate}" "#ifdef MANGOSBOT_ZERO\n        return true;\n#else\n        return false;\n#endif" "D28 vanilla-only alternative drop")

# D29: vanilla names - Searing Totem for Totem of Wrath, Healing Wave for Riptide, no dead factory keys.
require_text("${elemental}" "\"fire totem\",\n        NextAction::array(0, new NextAction(\"searing totem\", ACTION_HIGH), NULL)));" "D29 searing totem")
forbid_text("${elemental}" "NextAction(\"totem of wrath\"" "D29 the totem of wrath node")
require_text("${restoration}" "\"low health\",\n        NextAction::array(0, new NextAction(\"healing wave\", ACTION_CRITICAL_HEAL), NULL)));" "D29 healing wave")
require_text("${restoration}" "\"party member low health\",\n        NextAction::array(0, new NextAction(\"healing wave on party\", ACTION_CRITICAL_HEAL), NULL)));" "D29 healing wave on party")
forbid_text("${restoration}" "NextAction(\"riptide" "D29 a riptide node")
forbid_text("${restoration_full}" "creators[\"windfury\"]" "D29 the dead 'windfury' factory key")
forbid_text("${restoration_full}" "creators[\"chain heal on party\"]" "D29 the dead 'chain heal on party' factory key")

# D30: Innervate on the bot itself without a group, with the real mana percent (trigger and action).
file(READ "${PB_SOURCE_DIR}/strategy/druid/DruidTriggers.h" druid_triggers_h)
file(READ "${PB_SOURCE_DIR}/strategy/druid/DruidActions.h" druid_actions_h)
require_text("${druid_triggers_h}" "const bool validTarget = SpellTargetTrigger::IsTargetValid(target) ||\n                (target == bot && ai->IsSafe(bot) && !bot->IsDead() && !ai->HasAura(\"innervate\", bot));" "D30 trigger: the bot itself is a valid target")
require_text("${druid_actions_h}" "const bool validTarget = CastSpellTargetAction::IsTargetValid(target) ||\n                (target == bot && ai->IsSafe(bot) && !bot->IsDead() && !ai->HasAura(\"innervate\", bot));" "D30 action: the bot itself is a valid target")
count_text("${druid_triggers_h}${druid_actions_h}" "return currentMana > 0 && maxMana > 0 && currentMana * 100 / maxMana < sPlayerbotAIConfig.lowMana;" 2 "D30 real mana percent")

# D31: the critical-health basket is created under the key it is pushed as.
file(READ "${PB_SOURCE_DIR}/strategy/paladin/RetributionPaladinStrategy.cpp" ret_full)
require_text("${ret_full}" "creators[\"repentance or shield\"] = &repentance_or_shield;" "D31 repentance or shield key")
forbid_text("${ret_full}" "repentance of shield" "D31 the typo key")

# D41: Exorcism only against the creature types of its spell entry.
file(READ "${PB_SOURCE_DIR}/strategy/paladin/PaladinTriggers.cpp" paladin_triggers)
region("${paladin_triggers}" "bool ExorcismTrigger::IsActive()" "\n}" exorcism)
require_order("${exorcism}" "if (targetTypeMask && !(exorcismInfo->TargetCreatureType & targetTypeMask) && !exorcismTarget->HasAura(8179))\n                return false;" "return AI_VALUE2(uint8, \"mana\", \"self target\") >" "D41 creature type gate before the mana check")

# D42: no Seal of Vengeance check / node in vanilla.
region("${paladin_triggers}" "bool SealTrigger::IsActive()" "\n}\n" seal)
require_text("${seal}" "#ifdef MANGOSBOT_ZERO\n    const bool skipVengeanceCheck = true;\n#else\n    const bool skipVengeanceCheck = false;\n#endif" "D42 vanilla-only skip of the vengeance check")
read_vanilla(strategy/paladin/ProtectionPaladinStrategy.cpp prot_paladin)
require_text("${prot_paladin}" "\"seal\",\n        NextAction::array(0, new NextAction(\"seal of righteousness\", ACTION_NORMAL + 2), NULL)));" "D42 seal of righteousness")
forbid_text("${prot_paladin}" "NextAction(\"seal of vengeance\"" "D42 a seal of vengeance node in vanilla")

# D43 / D44: no Mangle (Cat / Bear) and no Swipe (Cat) in vanilla.
read_vanilla(strategy/druid/DpsFeralDruidStrategy.cpp dps_feral)
read_vanilla(strategy/druid/TankFeralDruidStrategy.cpp tank_feral)
require_text("${dps_feral}" "\"not behind target\",\n        NextAction::array(0, new NextAction(\"claw\", ACTION_NORMAL), NULL)));" "D43 claw for mangle (cat)")
forbid_text("${dps_feral}" "NextAction(\"mangle (cat)\", ACTION" "D43 a mangle (cat) node in vanilla")
require_text("${tank_feral}" "\"medium rage available\",\n        NextAction::array(0, new NextAction(\"maul\", ACTION_NORMAL + 1), NULL)));" "D43 maul for mangle (bear)")
forbid_text("${tank_feral}" "NextAction(\"mangle (bear)\", ACTION" "D43 a mangle (bear) node in vanilla")
file(READ "${PB_SOURCE_DIR}/strategy/druid/DpsFeralDruidStrategy.cpp" dps_feral_full)
count_text("${dps_feral_full}" "static bool DietNoMangle()\n    {\n#ifdef MANGOSBOT_ZERO\n        return true;\n#else\n        return false;\n#endif" 2 "D43 vanilla-only mangle alternative (cat factories)")
forbid_text("${dps_feral}" "NextAction(\"swipe (cat)\"" "D44 a swipe (cat) node in vanilla")

# D45: Nature's Grasp only while ready; Enrage only while known and ready.
read_vanilla(strategy/druid/BalanceDruidStrategy.cpp balance)
read_vanilla(strategy/druid/RestorationDruidStrategy.cpp resto_druid)
foreach(v balance resto_druid)
  require_text("${${v}}" "        \"nature's grasp ready\",\n        NextAction::array(0, new NextAction(\"nature's grasp\", ACTION_HIGH), NULL)));" "D45 nature's grasp on its ready trigger (${v})")
endforeach()
require_text("${druid_triggers_h}" "            uint32 const enrageId = AI_VALUE2(uint32, \"spell id\", \"enrage\");\n            if (!enrageId || !bot->IsSpellReady(enrageId))\n                return false;" "D45 enrage known and ready")

# D46: the leveling druid triggers wait for a known Rejuvenation / Moonfire.
file(READ "${PB_SOURCE_DIR}/strategy/druid/LevelingDruidStrategy.h" leveling_h)
require_text("${leveling_h}" "if (!AI_VALUE2(uint32, \"spell id\", \"rejuvenation\"))\n                return false;" "D46 rejuvenation known")
require_text("${leveling_h}" "if (!AI_VALUE2(uint32, \"spell id\", \"moonfire\"))\n                return false;" "D46 moonfire known")

message(STATUS "DIET_G3_SOURCE_CONTRACT=PASS")
