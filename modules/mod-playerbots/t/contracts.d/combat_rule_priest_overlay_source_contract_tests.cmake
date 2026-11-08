# A3 step 5 (ADR-0041; OB-00 07.11.2026, design A3-S5-KAMPFREGEL-ENTWURF.md): the combat rule, part PriestOverlay
# (AiPlayerbot.CombatRule.PriestOverlay, default 0; 1 all, 2 cohort B, 3 cohort A). AiFactory puts RandomBotCombatStrategies on
# a free bot's combat engine through the rule (ai::combat_rule::CombatOverlay); with the part off it returns the overlay as it is.
# With the part on, a discipline / holy priest (tab 0/1) gets it without the exact entry "+dps" (the shadow placeholder that
# replaces its tree, A3-AKTIONS-VERDRAHTUNG F1 (a)); "+dps assist" and all other entries stay. Pins: key default 0, > 3 = off,
# [ConfigEcho] sorted, the .dist entry; the policy (priest id 5, tabs 0/1, exact-entry removal without trimming, like
# Engine::ChangeStrategy); the glue (part off or no healing-tree priest = overlay unchanged, nothing counted); the one AiFactory
# call site and no direct overlay call left; the priest "dps" creator is still the shadow placeholder (the reason for the part);
# the minute line only with the part on. Every check runs once on the real sources and once per negative probe.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/CombatRulePolicy.h" policy)
read_file("${PB_SOURCE_DIR}/CombatRule.cpp" glue)
read_file("${PB_SOURCE_DIR}/AiFactory.cpp" factory)
read_file("${PB_SOURCE_DIR}/strategy/priest/PriestAiObjectContext.cpp" priest_ctx)
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

function(check policy glue factory priest_ctx config_h config_cpp dist out_var)
  set(errors "")

  # 1. Key: default 0, > 3 off, [ConfigEcho] in its sorted place, .dist entry.
  need("${dist}" "\nAiPlayerbot.CombatRule.PriestOverlay = 0\n" "dist key, default 0")
  need("${config_h}" "    uint32 combatRulePriestOverlay = 0;\n" "config member, default 0")
  need("${config_cpp}" "    combatRulePriestOverlay = uint32(config.GetIntDefault(\"AiPlayerbot.CombatRule.PriestOverlay\", 0));\n    if (combatRulePriestOverlay > 3)\n        combatRulePriestOverlay = 0;\n" "config read, > 3 = off")
  need("${config_cpp}" "            {\"CombatRule.HealerStart\", Value(combatRuleHealerStart)},\n            {\"CombatRule.PriestOverlay\", Value(combatRulePriestOverlay)},\n" "in [ConfigEcho], sorted after CombatRule.HealerStart (the key after it is not pinned: CombatRule.Progress*, P4, sorts in between)")

  # 2. The policy: healing-tree priests only, exact-entry removal.
  need("${policy}" "constexpr std::uint32_t ClassPriest = 5;" "priest class id as in SharedDefines.h")
  need("${policy}" "    return cls == ClassPriest && (tab == 0 || tab == 1);" "only discipline / holy priests")
  need("${policy}" "        if (entry != token)\n" "exact match, no trimming")
  ban("${policy}" "find(token" "a substring match (would also drop \"+dps assist\")")

  # 3. The glue: part off or no healing-tree priest = the overlay unchanged and nothing counted; "+dps" only where it applies.
  need("${glue}" "    uint32 const mode = sPlayerbotAIConfig.combatRulePriestOverlay;\n    // Part off, or no healing-tree priest: the overlay unchanged, nothing else is read.\n    if (!mode || !HealerPriest(player->getClass(), tab))\n        return overlay;\n" "glue: part off = overlay unchanged")
  need("${glue}" "    if (!ai::cohort::Applies(mode, guidLow))\n        return overlay;\n    c.stripped[cohort].fetch_add(1, std::memory_order_relaxed);\n    return WithoutEntry(overlay, \"+dps\");" "glue: only the cohort, only \"+dps\"")
  need("${glue}" "    if (!sPlayerbotAIConfig.combatRulePriestOverlay || now < lastReport + 60)\n        return;" "minute line only with the part on")
  need("${glue}" "\"[CombatRule] part=PriestOverlay mode=%u engines=%llu/%llu stripped=%llu/%llu\"" "minute line format")
  need("${glue}" "    ReportPriestOverlay();\n" "the part's minute line is reported")

  # 4. The one call site: the free-bot overlay goes through the rule; the explicit-strategy branch is unchanged.
  need("${factory}" "        combatEngine->ChangeStrategy(ai::combat_rule::CombatOverlay(player, tab, sPlayerbotAIConfig.randomBotCombatStrategies));" "AiFactory asks the rule")
  ban("${factory}" "combatEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies);" "the overlay applied around the rule")
  need("${factory}" "        combatEngine->ChangeStrategy(sPlayerbotAIConfig.combatStrategies);" "the non-free-bot branch unchanged")

  # 5. Why the part exists: "dps" is the shadow placeholder in the priest context.
  need("${priest_ctx}" "creators[\"dps\"] = [](PlayerbotAI* ai) { return new ShadowPriestPlaceholderStrategy(ai); };" "priest dps = shadow placeholder")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${glue}" "${factory}" "${priest_ctx}" "${config_h}" "${config_cpp}" "${dist}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "A3 step 5 combat rule PriestOverlay contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "PriestOverlay probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${glue}" "${factory}" "${priest_ctx}" "${config_h}" "${config_cpp}" "${dist}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "PriestOverlay probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.CombatRule.PriestOverlay = 0\n" "\nAiPlayerbot.CombatRule.PriestOverlay = 2\n")
probe("unknown mode kept" config_cpp "    if (combatRulePriestOverlay > 3)\n        combatRulePriestOverlay = 0;\n" "")
probe("echo missing" config_cpp "            {\"CombatRule.PriestOverlay\", Value(combatRulePriestOverlay)},\n" "")
probe("shadow priests too" policy "    return cls == ClassPriest && (tab == 0 || tab == 1);" "    return cls == ClassPriest;")
probe("trimmed match" policy "        if (entry != token)\n" "        if (entry.find(token) == std::string::npos)\n")
probe("every cohort" glue "    if (!ai::cohort::Applies(mode, guidLow))\n        return overlay;\n" "")
probe("wrong entry" glue "    return WithoutEntry(overlay, \"+dps\");" "    return WithoutEntry(overlay, \"+dps assist\");")
probe("call site bypasses the rule" factory "        combatEngine->ChangeStrategy(ai::combat_rule::CombatOverlay(player, tab, sPlayerbotAIConfig.randomBotCombatStrategies));" "        combatEngine->ChangeStrategy(sPlayerbotAIConfig.randomBotCombatStrategies);")
probe("minute line always on" glue "    if (!sPlayerbotAIConfig.combatRulePriestOverlay || now < lastReport + 60)" "    if (now < lastReport + 60)")
probe("report not called" glue "    ReportPriestOverlay();\n" "")

message(STATUS "combat_rule_priest_overlay source contract passed (${probes} negative probes)")
