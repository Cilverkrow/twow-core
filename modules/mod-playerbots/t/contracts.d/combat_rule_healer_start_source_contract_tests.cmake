# A3 step 5 (ADR-0041; OB-00 07.11.2026, design A3-S5-KAMPFREGEL-ENTWURF.md): the combat rule, part HealerStart
# (AiPlayerbot.CombatRule.HealerStart, default 0; 1 all, 2 cohort B, 3 cohort A). The healer gate of attack my target, attack RTI
# target and attack anything moves into the rule (ai::combat_rule::HealerMayAttack); with the part off it computes the old rule
# (no heal strategy, or "offdps" in the combat engine) and reads nothing else. With the part on, a holy paladin or a restoration
# druid - the two healers whose class context has no "offdps" creator - passes like a healer with offdps (F2 (a): the rule owns
# the bit, no new creators). Pins: key default 0, > 3 = off, in [ConfigEcho] sorted, the .dist entry; CombatRulePolicy.h pure;
# the old rule first in the policy and in the glue (part off = old expression); the three call sites ask the rule and keep no
# copy of the old expression; no offdps creator in the paladin / druid contexts; the [CombatRule] minute line only with the part
# on. Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/CombatRulePolicy.h" policy)
read_file("${PB_SOURCE_DIR}/CombatRule.cpp" glue)
read_file("${PB_SOURCE_DIR}/strategy/actions/AttackAction.cpp" attack_cpp)
read_file("${PB_SOURCE_DIR}/strategy/actions/ChooseTargetActions.cpp" choose_cpp)
read_file("${PB_SOURCE_DIR}/strategy/paladin/PaladinAiObjectContext.cpp" paladin_ctx)
read_file("${PB_SOURCE_DIR}/strategy/druid/DruidAiObjectContext.cpp" druid_ctx)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

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

set(old_gate "!ai->ContainsStrategy(STRATEGY_TYPE_HEAL) || ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT)")
set(old_gate_neg "ai->ContainsStrategy(STRATEGY_TYPE_HEAL) && !ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT)")

function(check policy glue attack_cpp choose_cpp paladin_ctx druid_ctx mgr_cpp config_h config_cpp dist out_var)
  set(errors "")

  # 1. Key: default 0, > 3 off, [ConfigEcho] in its sorted place, .dist entry.
  need("${dist}" "\nAiPlayerbot.CombatRule.HealerStart = 0\n" "dist key, default 0")
  need("${config_h}" "    uint32 combatRuleHealerStart = 0;\n" "config member, default 0")
  need("${config_cpp}" "    combatRuleHealerStart = uint32(config.GetIntDefault(\"AiPlayerbot.CombatRule.HealerStart\", 0));\n    if (combatRuleHealerStart > 3)\n        combatRuleHealerStart = 0;\n" "config read, > 3 = off")
  need("${config_cpp}" "            {\"CheatItem.Trace\", Value(cheatItemTrace)},\n            {\"CombatRule.HealerStart\", Value(combatRuleHealerStart)},\n            {\"CombatRule.PriestOverlay\"" "in [ConfigEcho], sorted (PriestOverlay follows)")

  # 2. The policy is pure and keeps the old rule first.
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc IN LISTS incs)
    if(NOT inc MATCHES "^#include <(atomic|cstdint|string)>$")
      list(APPEND errors "CombatRulePolicy.h is not pure: ${inc}")
    endif()
  endforeach()
  need("${policy}" "inline bool HealerMayAttack(bool hasHealStrategy, bool hasOffdps, bool partApplies, bool healerWithoutOffdpsCreator)\n{\n    if (!hasHealStrategy || hasOffdps)\n        return true;\n    return partApplies && healerWithoutOffdpsCreator;\n}" "policy: the old rule first, the part only adds")
  need("${policy}" "    return (cls == ClassPaladin && holy) || (cls == ClassDruid && restoration);" "policy: only holy paladin / resto druid")
  need("${policy}" "constexpr std::uint32_t ClassPaladin = 2;\nconstexpr std::uint32_t ClassDruid = 11;" "class ids as in SharedDefines.h")

  # 3. The glue: part off = the old expression, nothing else read; the healer facts from the combat engine.
  need("${glue}" "    bool const heal = ai->ContainsStrategy(STRATEGY_TYPE_HEAL);\n    bool const offdps = ai->HasStrategy(\"offdps\", BotState::BOT_STATE_COMBAT);\n    uint32 const mode = sPlayerbotAIConfig.combatRuleHealerStart;\n" "glue: the old rule's facts")
  need("${glue}" "    if (!mode || !heal || offdps)\n        return HealerMayAttack(heal, offdps, false, false);\n" "glue: part off or old rule passes = old expression")
  need("${glue}" "ai->HasStrategy(\"holy\", BotState::BOT_STATE_COMBAT)" "glue: holy from the combat engine")
  need("${glue}" "ai->HasStrategy(\"restoration\", BotState::BOT_STATE_COMBAT)" "glue: restoration from the combat engine")
  need("${glue}" "    bool const applies = ai::cohort::Applies(mode, guidLow);" "glue: cohort modes")
  need("${glue}" "    if (!sPlayerbotAIConfig.combatRuleHealerStart || now < lastReport + 60)\n        return;" "minute line only with the part on")
  need("${glue}" "\"[CombatRule] part=HealerStart mode=%u asked=%llu/%llu lifted=%llu/%llu cls=%s\"" "minute line format")

  # 4. The three call sites ask the rule and keep no copy of the old expression.
  need("${attack_cpp}" "bool AttackMyTargetAction::isUseful()\n{\n    return ai::combat_rule::HealerMayAttack(ai);" "attack my target asks the rule")
  need("${attack_cpp}" "bool AttackRTITargetAction::isUseful()\n{\n    return ai::combat_rule::HealerMayAttack(ai);" "attack RTI target asks the rule")
  need("${choose_cpp}" "    if (!ai::combat_rule::HealerMayAttack(ai))" "attack anything asks the rule")
  ban("${attack_cpp}" "${old_gate}" "a copy of the old gate in AttackAction.cpp")
  ban("${choose_cpp}" "${old_gate_neg}" "a copy of the old gate in ChooseTargetActions.cpp")

  # 5. F2 (a): no new offdps creators for paladin / druid.
  ban("${paladin_ctx}" "creators[\"offdps\"]" "a paladin offdps creator (the rule owns the bit)")
  ban("${druid_ctx}" "creators[\"offdps\"]" "a druid offdps creator (the rule owns the bit)")

  # 6. The minute pass calls the module's report.
  need("${mgr_cpp}" "    ai::combat_rule::ReportCombatRule();" "minute report call")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${glue}" "${attack_cpp}" "${choose_cpp}" "${paladin_ctx}" "${druid_ctx}" "${mgr_cpp}" "${config_h}" "${config_cpp}" "${dist}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "A3 step 5 combat rule HealerStart contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "HealerStart probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${glue}" "${attack_cpp}" "${choose_cpp}" "${paladin_ctx}" "${druid_ctx}" "${mgr_cpp}" "${config_h}" "${config_cpp}" "${dist}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "HealerStart probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.CombatRule.HealerStart = 0\n" "\nAiPlayerbot.CombatRule.HealerStart = 1\n")
probe("unknown mode kept" config_cpp "    if (combatRuleHealerStart > 3)\n        combatRuleHealerStart = 0;\n" "")
probe("echo missing" config_cpp "            {\"CombatRule.HealerStart\", Value(combatRuleHealerStart)},\n" "")
probe("policy impure" policy "#include <cstdint>" "#include <cstdint>\n#include \"PlayerbotAI.h\"")
probe("policy drops the old rule" policy "    if (!hasHealStrategy || hasOffdps)\n        return true;\n" "")
probe("policy opens every healer" policy "    return partApplies && healerWithoutOffdpsCreator;" "    return partApplies;")
probe("glue reads more with the part off" glue "    if (!mode || !heal || offdps)\n" "    if (!heal || offdps)\n")
probe("old gate left at a call site" attack_cpp "bool AttackRTITargetAction::isUseful()\n{\n    return ai::combat_rule::HealerMayAttack(ai);" "bool AttackRTITargetAction::isUseful()\n{\n    return ${old_gate};")
probe("attack anything bypasses the rule" choose_cpp "    if (!ai::combat_rule::HealerMayAttack(ai))" "    if (${old_gate_neg})")
probe("paladin offdps creator" paladin_ctx "creators[\"holy\"] = " "creators[\"offdps\"] = [](PlayerbotAI* ai) { return new OffdpsPlaceholderStrategy(ai); };\n                creators[\"holy\"] = ")
probe("minute line always on" glue "    if (!sPlayerbotAIConfig.combatRuleHealerStart || now < lastReport + 60)" "    if (now < lastReport + 60)")
probe("report not called" mgr_cpp "    ai::combat_rule::ReportCombatRule();" "")

message(STATUS "combat_rule_healer_start source contract passed (${probes} negative probes)")
