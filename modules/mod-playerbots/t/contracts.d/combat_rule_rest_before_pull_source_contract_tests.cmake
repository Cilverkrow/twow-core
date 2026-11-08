# A3 step 5 (ADR-0041; OB-00 07.11.2026, design A3-S5-KAMPFREGEL-ENTWURF.md T2): the combat rule, part RestBeforePull
# (AiPlayerbot.CombatRule.RestBeforePull, default 0; 1 all, 2 cohort B, 3 cohort A; RestStartHp 60, RestStartMana 40). The rule owns
# the pull bit of attack anything and has its own rest node (strategy "combat rule": food / drink at 7.0 while it says rest first,
# no lower HP bound). Not RestGate: no "can move around" lock, no episode state, and a bot RestGate applies to is left to RestGate.
# Pins: keys and clamps, [ConfigEcho] sorted, .dist; the pure policy (solo free, out of combat, not attacked; health first; mana only
# with a pool; self-defence always allowed); the glue (part off = nothing read, RestGate exclusion, cohort); the strategy and its
# two triggers registered, relevance 7.0 above choose travel target 6.98; AiFactory adds the node only where the part applies; the
# pull gate in attack anything after the healer gate; "should eat" opens only through the rule's health need; no "can move around"
# write; the minute line only with the part on. Every check runs once on the real sources and once per negative probe.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/CombatRulePolicy.h" policy)
read_file("${PB_SOURCE_DIR}/CombatRule.cpp" glue)
read_file("${PB_SOURCE_DIR}/strategy/generic/CombatRuleStrategy.cpp" strat)
read_file("${PB_SOURCE_DIR}/strategy/StrategyContext.h" strat_ctx)
read_file("${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trig_ctx)
read_file("${PB_SOURCE_DIR}/AiFactory.cpp" factory)
read_file("${PB_SOURCE_DIR}/strategy/actions/ChooseTargetActions.cpp" choose_cpp)
read_file("${PB_SOURCE_DIR}/strategy/values/MaintenanceValues.h" maint)
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

function(check policy glue strat strat_ctx trig_ctx factory choose_cpp maint config_h config_cpp dist out_var)
  set(errors "")

  # 1. Keys: mode default 0 and > 3 off; StartHp 60 (1..99), StartMana 40 (0..99); [ConfigEcho] sorted; .dist.
  need("${dist}" "\nAiPlayerbot.CombatRule.RestBeforePull = 0\n" "dist key, default 0")
  need("${dist}" "\nAiPlayerbot.CombatRule.RestStartHp = 60\n" "dist StartHp 60")
  need("${dist}" "\nAiPlayerbot.CombatRule.RestStartMana = 40\n" "dist StartMana 40")
  need("${config_h}" "    uint32 combatRuleRestBeforePull = 0;\n    uint32 combatRuleRestStartHp = 60;     // 1..99\n    uint32 combatRuleRestStartMana = 40;   // 0..99, 0 = no mana rest\n" "config members")
  need("${config_cpp}" "    combatRuleRestBeforePull = uint32(config.GetIntDefault(\"AiPlayerbot.CombatRule.RestBeforePull\", 0));\n    if (combatRuleRestBeforePull > 3)\n        combatRuleRestBeforePull = 0;\n    combatRuleRestStartHp = ai::combat_rule::ClampStartHp(config.GetIntDefault(\"AiPlayerbot.CombatRule.RestStartHp\", 60));\n    combatRuleRestStartMana = ai::combat_rule::ClampStartMana(config.GetIntDefault(\"AiPlayerbot.CombatRule.RestStartMana\", 40));\n" "config read and clamps")
  need("${config_cpp}" "            {\"CombatRule.RestBeforePull\", Value(combatRuleRestBeforePull)},\n            {\"CombatRule.RestStartHp\", Value(combatRuleRestStartHp)},\n            {\"CombatRule.RestStartMana\", Value(combatRuleRestStartMana)},\n" "in [ConfigEcho], sorted (the neighbours are not pinned: CombatRule.Progress*, P4, before it; Crafting.Consumables, #908, after it)")
  need("${policy}" "    return value < 1 ? 1u : (value > 99 ? 99u : std::uint32_t(value));" "StartHp clamp 1..99")
  need("${policy}" "    return value < 0 ? 0u : (value > 99 ? 99u : std::uint32_t(value));" "StartMana clamp 0..99")

  # 2. The pure rule.
  need("${policy}" "    if (!r.soloFree || r.inCombat || r.attacked)\n        return RestNeed::None;\n    if (r.hp < startHp)\n        return RestNeed::Health;\n    if (r.manaPool && r.mana < startMana)\n        return RestNeed::Mana;\n    return RestNeed::None;" "policy: solo free out of combat, health first, mana with a pool")
  need("${policy}" "    return need == RestNeed::None || targetFightsBot;" "policy: self-defence always allowed")

  # 3. The glue: part off = nothing read; cohort; RestGate's bots left to RestGate; the solo free reading as RestGate's.
  need("${glue}" "    uint32 const mode = sPlayerbotAIConfig.combatRuleRestBeforePull;\n    if (!mode)\n        return RestNeed::None;" "glue: part off = no need")
  need("${glue}" "    if (!ai::cohort::Applies(mode, guidLow) || ai::cohort::Applies(sPlayerbotAIConfig.rotationRestGate, guidLow))\n        return RestNeed::None;" "glue: cohort, RestGate exclusion")
  need("${glue}" "    return !bot->GetGroup() && !ai->IsRealPlayer() && !ai->HasRealPlayerMaster() && sRandomPlayerbotMgr.IsFreeBot(bot) &&\n        !bot->InBattleGround() && bot->IsAlive();" "glue: solo free bots only")
  need("${glue}" "bool MayPull(PlayerbotAI* ai, Unit* target)\n{\n    if (!sPlayerbotAIConfig.combatRuleRestBeforePull)\n        return true;" "glue: pull bit off = true, nothing read")
  need("${glue}" "    bool const ok = MayPull(need, target && target->GetVictim() == bot);" "glue: a target fighting the bot is taken")
  need("${glue}" "void CountPull(PlayerbotAI* ai)\n{\n    if (!sPlayerbotAIConfig.combatRuleRestBeforePull)\n        return;" "glue: pull count only with the part on")
  need("${glue}" "    if (!sPlayerbotAIConfig.combatRuleRestBeforePull || now < lastReport + 60)\n        return;" "minute line only with the part on")
  need("${glue}" "    ReportRestBeforePull();\n" "the part's minute line is reported")
  ban("${glue}" "can move around" "a movement lock (that is RestGate)")

  # 4. The rule's rest node: strategy + triggers registered, food / drink at 7.0.
  need("${strat}" "        \"combat rule rest health\",\n        NextAction::array(0, new NextAction(\"food\", 7.0f), NULL)));" "rest node: food at 7.0")
  need("${strat}" "        \"combat rule rest mana\",\n        NextAction::array(0, new NextAction(\"drink\", 7.0f), NULL)));" "rest node: drink at 7.0")
  need("${strat}" "    if (ai::combat_rule::RestBeforePullNeed(ai) != ai::combat_rule::RestNeed::Health)\n        return false;" "health trigger reads the rule")
  need("${strat}" "    if (ai::combat_rule::RestBeforePullNeed(ai) != ai::combat_rule::RestNeed::Mana)\n        return false;" "mana trigger reads the rule")
  need("${strat_ctx}" "creators[\"combat rule\"] = [](PlayerbotAI* ai) { return new CombatRuleStrategy(ai); };" "strategy registered")
  need("${trig_ctx}" "creators[\"combat rule rest health\"] = [](PlayerbotAI* ai) { return new CombatRuleRestHealthTrigger(ai); };" "health trigger registered")
  need("${trig_ctx}" "creators[\"combat rule rest mana\"] = [](PlayerbotAI* ai) { return new CombatRuleRestManaTrigger(ai); };" "mana trigger registered")

  # 5. AiFactory adds the node only where the part applies; the pull gate in attack anything; "should eat" through the rule only.
  need("${factory}" "    if (ai::cohort::Applies(sPlayerbotAIConfig.combatRuleRestBeforePull, player->GetGUIDLow()) && !player->InBattleGround())\n        nonCombatEngine->addStrategy(\"combat rule\");" "AiFactory: node only where the part applies")
  string(REGEX MATCHALL "addStrategy\\(\"combat rule\"\\)" adds "${factory}")
  list(LENGTH adds n_adds)
  if(NOT n_adds EQUAL 1)
    list(APPEND errors "AiFactory adds \"combat rule\" ${n_adds} times (want exactly 1, gated)")
  endif()
  need("${choose_cpp}" "    if (!ai::combat_rule::HealerMayAttack(ai))   // A3 step 5: the healer gate (CombatRule.HealerStart, default 0 = the old rule)\n        return false;\n\n    // A3 step 5 (CombatRule.RestBeforePull, default 0 = no check): the pull bit - no pull while the rule says rest first, unless the\n    // target already fights the bot.\n    if (!ai::combat_rule::MayPull(ai, target))\n        return false;" "pull gate after the healer gate")
  need("${choose_cpp}" "bool ai::AttackAnythingAction::Execute(Event& event)\n{\n    ai::combat_rule::CountPull(ai);" "pull counted at the start of the pull")
  need("${maint}" "            if (sPlayerbotAIConfig.combatRuleRestBeforePull && ai::combat_rule::RestBeforePullNeed(ai) == ai::combat_rule::RestNeed::Health)\n                return true;" "should eat: the rule's health need only, part off = nothing read")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${glue}" "${strat}" "${strat_ctx}" "${trig_ctx}" "${factory}" "${choose_cpp}" "${maint}" "${config_h}" "${config_cpp}" "${dist}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "A3 step 5 combat rule RestBeforePull contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "RestBeforePull probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${glue}" "${strat}" "${strat_ctx}" "${trig_ctx}" "${factory}" "${choose_cpp}" "${maint}" "${config_h}" "${config_cpp}" "${dist}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "RestBeforePull probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.CombatRule.RestBeforePull = 0\n" "\nAiPlayerbot.CombatRule.RestBeforePull = 1\n")
probe("unknown mode kept" config_cpp "    if (combatRuleRestBeforePull > 3)\n        combatRuleRestBeforePull = 0;\n" "")
probe("echo missing" config_cpp "            {\"CombatRule.RestStartHp\", Value(combatRuleRestStartHp)},\n" "")
probe("rest in combat" policy "    if (!r.soloFree || r.inCombat || r.attacked)\n" "    if (!r.soloFree || r.attacked)\n")
probe("no self-defence" policy "    return need == RestNeed::None || targetFightsBot;" "    return need == RestNeed::None;")
probe("RestGate overlap" glue " || ai::cohort::Applies(sPlayerbotAIConfig.rotationRestGate, guidLow))" ")")
probe("groups too" glue "    return !bot->GetGroup() && !ai->IsRealPlayer()" "    return !ai->IsRealPlayer()")
probe("pull bit reads with the part off" glue "bool MayPull(PlayerbotAI* ai, Unit* target)\n{\n    if (!sPlayerbotAIConfig.combatRuleRestBeforePull)\n        return true;\n" "bool MayPull(PlayerbotAI* ai, Unit* target)\n{\n")
probe("movement lock" glue "    ReportRestBeforePull();\n" "    ReportRestBeforePull();\n    // can move around\n")
probe("below travel" strat "new NextAction(\"food\", 7.0f)" "new NextAction(\"food\", 6.0f)")
probe("node for every bot" factory "    if (ai::cohort::Applies(sPlayerbotAIConfig.combatRuleRestBeforePull, player->GetGUIDLow()) && !player->InBattleGround())\n        nonCombatEngine->addStrategy(\"combat rule\");" "    nonCombatEngine->addStrategy(\"combat rule\");")
probe("pull gate missing" choose_cpp "    if (!ai::combat_rule::MayPull(ai, target))\n        return false;" "")
probe("should eat always" maint "            if (sPlayerbotAIConfig.combatRuleRestBeforePull && ai::combat_rule::RestBeforePullNeed(ai) == ai::combat_rule::RestNeed::Health)" "            if (ai::combat_rule::RestBeforePullNeed(ai) == ai::combat_rule::RestNeed::Health)")
probe("trigger unregistered" trig_ctx "creators[\"combat rule rest mana\"]" "creators[\"combat rule rest mana x\"]")
probe("minute line always on" glue "    if (!sPlayerbotAIConfig.combatRuleRestBeforePull || now < lastReport + 60)" "    if (now < lastReport + 60)")

message(STATUS "combat_rule_rest_before_pull source contract passed (${probes} negative probes)")
