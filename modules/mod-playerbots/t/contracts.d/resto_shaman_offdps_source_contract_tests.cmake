# twow-repo#653 side finding (OB-00 31.10.2026): AiPlayerbot.Rotation.RestoShamanOffdps (default 0; 1 all, 2 cohort B, 3 cohort A for
# the crossover) gives a restoration shaman the offdps placeholder - the shaman context had no creator, so the engine dropped the
# strategy AiFactory added and the healer gate refused every attack.
# Pins: key default 0, > 3 = off, in [ConfigEcho] sorted; the shaman creator; AiFactory adds offdps to a resto shaman only through the
# cohort switch (no ungated add left in the shaman case). OB-00 condition 1 (heal priority unchanged in a group): offdps stays an
# empty placeholder (no triggers, no actions, no multiplier), no shaman strategy / action / trigger reads it, and its readers stay the
# three attack gates (attack my target, attack RTI target, attack anything). Every check runs once on the real sources and once per
# negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/AiFactory.cpp" factory)
read_file("${PB_SOURCE_DIR}/strategy/shaman/ShamanAiObjectContext.cpp" context)
read_file("${PB_SOURCE_DIR}/strategy/generic/ClassStrategy.h" class_h)
read_file("${PB_SOURCE_DIR}/strategy/actions/AttackAction.cpp" attack_cpp)
read_file("${PB_SOURCE_DIR}/strategy/actions/ChooseTargetActions.cpp" choose_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
set(shaman_all "")
foreach(f RestorationShamanStrategy.cpp RestorationShamanStrategy.h ShamanStrategy.cpp ShamanStrategy.h ShamanActions.cpp ShamanActions.h
          ShamanTriggers.cpp ShamanTriggers.h ShamanMultipliers.cpp ShamanMultipliers.h)
  read_file("${PB_SOURCE_DIR}/strategy/shaman/${f}" t)
  string(APPEND shaman_all "${t}")
endforeach()

# text from `start` up to (not including) `stop`, empty when either is missing
function(between text start stop out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${stop}" b)
  if(b EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

function(need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(errors "${errors};missing ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(ban text needle what)
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    set(errors "${errors};forbidden ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()

function(check factory context class_h attack_cpp choose_cpp config_h config_cpp dist shaman_all out_var)
  set(errors "")

  # 1. Key: default 0, > 3 off, [ConfigEcho] in its sorted place.
  need("${dist}" "\nAiPlayerbot.Rotation.RestoShamanOffdps = 0\n" "dist key, default 0")
  need("${config_h}" "uint32 rotationRestoShamanOffdps = 0;" "config member, default 0")
  need("${config_cpp}" "rotationRestoShamanOffdps = config.GetIntDefault(\"AiPlayerbot.Rotation.RestoShamanOffdps\", 0);   // twow-repo#653 resto shaman offdps\n    if (rotationRestoShamanOffdps > 3)\n        rotationRestoShamanOffdps = 0;" "config read, > 3 = off")
  need("${config_cpp}" "            {\"Rotation.RestGate\", Value(rotationRestGate)},\n            {\"Rotation.RestoShamanOffdps\", Value(rotationRestoShamanOffdps)},\n            {\"Rotation.RogueTankStance\"" "in [ConfigEcho], sorted (owner rule #615, #710)")

  # 2. The creator and the gated add.
  need("${context}" "creators[\"offdps\"] = [](PlayerbotAI* ai) { return new OffdpsPlaceholderStrategy(ai); };" "shaman offdps creator")
  # the combat-strategy switch's shaman case (AiFactory has several "case CLASS_SHAMAN:"; this one starts with the elemental tab)
  between("${factory}" "        case CLASS_SHAMAN:\n        {\n            if (tab == 0)\n            {\n                combatEngine->addStrategies(\"elemental\", \"aoe\", \"cc\", \"flee\", \"ranged\", NULL);" "\n        case CLASS_" shaman_case)
  if(shaman_case STREQUAL "")
    list(APPEND errors "AiFactory shaman case not found")
  endif()
  need("${shaman_case}" "                combatEngine->addStrategies(\"restoration\", \"flee\", \"ranged\", NULL);" "resto strategies")
  need("${shaman_case}" "                if (sPlayerbotAIConfig.enableOffSpecStrategies && ai::cohort::Applies(sPlayerbotAIConfig.rotationRestoShamanOffdps, player->GetGUIDLow()))\n                    combatEngine->addStrategy(\"offdps\");" "offdps only through the cohort switch")
  ban("${shaman_case}" "                if (sPlayerbotAIConfig.enableOffSpecStrategies)\n                    combatEngine->addStrategy(\"offdps\");" "an ungated offdps add for shamans")

  # 3. OB-00 condition 1: the placeholder is empty, no shaman code reads it, its readers are the three attack gates.
  need("${class_h}" "    class PlaceholderStrategy : public Strategy\n    {\n    public:\n        PlaceholderStrategy(PlayerbotAI* ai) : Strategy(ai) {}\n    };" "an empty placeholder base")
  need("${class_h}" "    class OffdpsPlaceholderStrategy : public PlaceholderStrategy\n    {\n    public:\n        OffdpsPlaceholderStrategy(PlayerbotAI* ai) : PlaceholderStrategy(ai) {}\n        std::string getName() override { return \"offdps\"; }\n    };" "offdps without triggers, actions or multipliers")
  ban("${shaman_all}" "offdps" "a shaman strategy, action, trigger or multiplier reading offdps (heal priority)")
  need("${attack_cpp}" "bool AttackMyTargetAction::isUseful()\n{\n    return !ai->ContainsStrategy(STRATEGY_TYPE_HEAL) || ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT);\n}" "attack my target gate")
  need("${attack_cpp}" "bool AttackRTITargetAction::isUseful()\n{\n    return !ai->ContainsStrategy(STRATEGY_TYPE_HEAL) || ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT);\n}" "attack RTI target gate")
  need("${choose_cpp}" "    if (ai->ContainsStrategy(STRATEGY_TYPE_HEAL) && !ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT))\n        return false;" "attack anything gate")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${factory}" "${context}" "${class_h}" "${attack_cpp}" "${choose_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${shaman_all}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#653 resto shaman offdps contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#653 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${factory}" "${context}" "${class_h}" "${attack_cpp}" "${choose_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${shaman_all}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#653 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.Rotation.RestoShamanOffdps = 0\n" "\nAiPlayerbot.Rotation.RestoShamanOffdps = 1\n")
probe("unknown mode kept" config_cpp "    if (rotationRestoShamanOffdps > 3)\n        rotationRestoShamanOffdps = 0;" "")
probe("echo missing" config_cpp "            {\"Rotation.RestoShamanOffdps\", Value(rotationRestoShamanOffdps)},\n" "")
probe("creator missing" context "creators[\"offdps\"] = [](PlayerbotAI* ai) { return new OffdpsPlaceholderStrategy(ai); };" "")
probe("ungated add" factory " && ai::cohort::Applies(sPlayerbotAIConfig.rotationRestoShamanOffdps, player->GetGUIDLow()))\n                    combatEngine->addStrategy(\"offdps\");" ")\n                    combatEngine->addStrategy(\"offdps\");")
probe("placeholder with triggers" class_h "        std::string getName() override { return \"offdps\"; }\n" "        std::string getName() override { return \"offdps\"; }\n        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override {}\n")
probe("resto reads offdps" shaman_all "RestorationShaman" "RestorationShaman /* offdps */")
probe("heal gate changed" choose_cpp "    if (ai->ContainsStrategy(STRATEGY_TYPE_HEAL) && !ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT))" "    if (!ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT))")

message(STATUS "resto_shaman_offdps source contract passed (${probes} negative probes)")
