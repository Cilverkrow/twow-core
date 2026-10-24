# twow-repo#602 ROT-01 3c (priest Farathil loop, OB-00 24.10.2026): AiPlayerbot.Rotation.DarkRuneGate (default 0, behaviour change)
# pushes "dark rune" only while the rune or its node alternative "mana potion" is usable, and a failed check waits RecheckSeconds.
# Pins: switch default 0; off = the old "low mana" trigger node; the gate asks the actions themselves (both of them); the wait;
# the rune's required level only under the switch; the #485 consumables code untouched; report; pure policy.
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/DarkRuneGatePolicy.h" policy)
read_file("${PB_SOURCE_DIR}/strategy/generic/UsePotionsStrategy.cpp" strat)
read_file("${PB_SOURCE_DIR}/strategy/triggers/GenericTriggers.cpp" trig_cpp)
read_file("${PB_SOURCE_DIR}/strategy/triggers/GenericTriggers.h" trig_h)
read_file("${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trig_ctx)
read_file("${PB_SOURCE_DIR}/strategy/actions/UseItemAction.h" item_h)
read_file("${PB_SOURCE_DIR}/strategy/actions/UseItemAction.cpp" item_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr_cpp)

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

# append to `errors` of the calling function instead of failing (functions, not macros: macro arguments would
# re-evaluate the backslashes of the source text)
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
function(before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(errors "${errors};order ${what}: ${first} before ${second}" PARENT_SCOPE)
  endif()
endfunction()

function(check policy strat trig_cpp trig_h trig_ctx item_h item_cpp config_h config_cpp dist mgr_cpp out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho].
  need("${dist}" "\nAiPlayerbot.Rotation.DarkRuneGate = 0\n" "dist key, default 0")
  need("${dist}" "\nAiPlayerbot.Rotation.DarkRuneGate.RecheckSeconds = 10\n" "dist wait, default 10")
  need("${config_h}" "uint32 rotationDarkRuneGate = 0;" "config member, default 0")
  need("${config_h}" "uint32 rotationDarkRuneGateRecheckSeconds = 10;" "wait member, default 10")
  need("${config_cpp}" "rotationDarkRuneGate = config.GetIntDefault(\"AiPlayerbot.Rotation.DarkRuneGate\", 0);   // twow-repo#602 ROT-01 3c\n    if (rotationDarkRuneGate > 2)\n        rotationDarkRuneGate = 0;" "config read, > 2 = off")
  need("${config_cpp}" "rotationDarkRuneGateRecheckSeconds = config.GetIntDefault(\"AiPlayerbot.Rotation.DarkRuneGate.RecheckSeconds\", 10);" "wait read, default 10")
  need("${config_cpp}" "{\"Rotation.DarkRuneGate\", Value(rotationDarkRuneGate)}," "parsed mode in [ConfigEcho] (owner rule #615)")

  # 2. Pure policy (std + the cohort rule only).
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
      list(APPEND errors "DarkRuneGatePolicy.h is not pure: ${inc}")
    endif()
  endforeach()
  need("${policy}" "return ai::cohort::Applies(mode, guidLow);" "cohort rule")
  need("${policy}" "return now >= nextCheck;" "due rule")
  need("${policy}" "return now + std::int64_t(recheckSeconds);" "next check rule")
  need("${policy}" "return botLevel >= requiredLevel;" "level rule")

  # 3. Strategy: the same node behind either trigger; off = "low mana" as before.
  need("${strat}" "bool const darkRuneGate = ai::dark_rune_gate::Applies(sPlayerbotAIConfig.rotationDarkRuneGate, ai->GetBot()->GetGUIDLow());" "cohort mode in the strategy")
  need("${strat}" "        darkRuneGate ? \"low mana consumable\" : \"low mana\",\n        NextAction::array(0, new NextAction(\"dark rune\", ACTION_HIGH), NULL)));" "off = low mana, same dark rune node")
  need("${strat}" "ACTION_NODE_A(dark_rune, \"dark rune\", \"mana potion\");" "mana potion stays the rune's alternative")

  # 4. Trigger: registered; the wait first; low mana as LowManaTrigger; both actions asked via isPossible and isUseful; a miss waits.
  need("${trig_ctx}" "creators[\"low mana consumable\"] = [](PlayerbotAI* ai) { return new LowManaConsumableTrigger(ai); };" "trigger registered")
  need("${trig_h}" "LowManaConsumableTrigger(PlayerbotAI* ai) : Trigger(ai, \"low mana consumable\") {}" "trigger name")
  between("${trig_cpp}" "bool LowManaConsumableTrigger::IsActive()" "\n}\n" gate)
  before("${gate}" "if (!drg::Due(now, nextCheck))" "AI_VALUE2(uint8, \"mana\", \"self target\") < sPlayerbotAIConfig.lowMana" "the wait before any check")
  need("${gate}" "AI_VALUE2(bool, \"has mana\", \"self target\") && AI_VALUE2(uint8, \"mana\", \"self target\") < sPlayerbotAIConfig.lowMana" "low mana as LowManaTrigger")
  need("${gate}" "for (char const* name : { \"dark rune\", \"mana potion\" })" "rune and potion")
  need("${gate}" "if (action && action->isPossible() && action->isUseful())" "the actions decide")
  need("${gate}" "nextCheck = drg::NextCheck(now, sPlayerbotAIConfig.rotationDarkRuneGateRecheckSeconds);\n    return false;" "a miss waits RecheckSeconds")
  between("${trig_cpp}" "bool LowManaTrigger::IsActive()" "\n}\n" lowmana)
  need("${lowmana}" "return AI_VALUE2(bool, \"has mana\", \"self target\") && AI_VALUE2(uint8, \"mana\", \"self target\") < sPlayerbotAIConfig.lowMana;" "LowManaTrigger unchanged")
  ban("${lowmana}" "dark_rune_gate" "the gate inside low mana")

  # 5. Action: the rune's required level only under the switch; item, class and health rules unchanged.
  between("${item_h}" "class UseDarkRuneAction : public UseItemIdAction" "    };\n" rune)
  need("${rune}" "if (ai::dark_rune_gate::Applies(sPlayerbotAIConfig.rotationDarkRuneGate, bot->GetGUIDLow()))" "level check under the switch")
  need("${rune}" "if (!proto || !ai::dark_rune_gate::LevelOk(bot->GetLevel(), proto->RequiredLevel))\n                    return false;" "required level")
  need("${rune}" "if (bot->getClass() == CLASS_MAGE) // mage should use mana gem, shares cd with dark rune\n                return false;" "mage rule unchanged")
  need("${rune}" "return bot->GetHealth() > 1000;" "health rule unchanged")
  need("${rune}" "virtual uint32 GetItemId() override { return 20520; }" "item unchanged")

  # 6. The #485 consumables code (UseItemIdAction, potions, UseReal) is not touched by this switch.
  ban("${item_cpp}" "rotationDarkRuneGate" "the switch in UseItemAction.cpp (#485 code)")
  ban("${item_cpp}" "dark_rune_gate" "the policy in UseItemAction.cpp (#485 code)")

  # 7. Report once per minute after [ReachLos], nothing when off.
  before("${mgr_cpp}" "    ReportReachLos();" "    ReportDarkRuneGate();" "report after ReportReachLos")
  need("${mgr_cpp}" "if (!sPlayerbotAIConfig.rotationDarkRuneGate || now < lastReport + 60)" "report only when on")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${strat}" "${trig_cpp}" "${trig_h}" "${trig_ctx}" "${item_h}" "${item_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#602 dark rune gate contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#602 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${strat}" "${trig_cpp}" "${trig_h}" "${trig_ctx}" "${item_h}" "${item_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#602 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.Rotation.DarkRuneGate = 0\n" "\nAiPlayerbot.Rotation.DarkRuneGate = 1\n")
probe("unknown mode kept" config_cpp "    if (rotationDarkRuneGate > 2)\n        rotationDarkRuneGate = 0;" "")
probe("off path changed" strat "darkRuneGate ? \"low mana consumable\" : \"low mana\"," "\"low mana consumable\",")
probe("potion not counted" trig_cpp "{ \"dark rune\", \"mana potion\" }" "{ \"dark rune\" }")
probe("only isPossible" trig_cpp "action->isPossible() && action->isUseful()" "action->isPossible()")
probe("no wait" trig_cpp "nextCheck = drg::NextCheck(now, sPlayerbotAIConfig.rotationDarkRuneGateRecheckSeconds);\n" "")
probe("wait after the check" trig_cpp "    if (!drg::Due(now, nextCheck))\n    {\n        drg::waited.fetch_add(1, std::memory_order_relaxed);\n        return false;\n    }\n" "")
probe("level check always" item_h "            if (ai::dark_rune_gate::Applies(sPlayerbotAIConfig.rotationDarkRuneGate, bot->GetGUIDLow()))\n" "            if (true)\n")
probe("485 code touched" item_cpp "bool UseItemIdAction::isPossible()\n{" "bool UseItemIdAction::isPossible()\n{\n    // rotationDarkRuneGate")
probe("impure policy" policy "#include <cstdint>" "#include <cstdint>\n#include \"Entities/Player.h\"")
probe("report always" mgr_cpp "if (!sPlayerbotAIConfig.rotationDarkRuneGate || now < lastReport + 60)" "if (now < lastReport + 60)")
probe("low mana changed" trig_cpp "< sPlayerbotAIConfig.lowMana;\n}" "< sPlayerbotAIConfig.lowMana && ai::dark_rune_gate::Due(0, 0);\n}")

message(STATUS "dark_rune_gate source contract passed (${probes} negative probes)")
