# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A17, card 1, BEHAVIOUR CHANGE, default 0): behind AiPlayerbot.Perf.CombatIdleYield a combat
# AI pass that executed no action while the bot auto-attacks its current target (melee swing with the victim in
# melee reach, auto shot, wand) waits 3x ReactDelay instead of ReactDelay. Not for bots with a heal strategy, bots
# with a real player master or while casting. The wait ends early when the victim changes, leaves melee reach or the
# auto attack stops. Off: decisions and timing unchanged. Pure part: t/combat_idle_policy_tests.cpp.
#
# Inputs: -DPB_SOURCE_DIR=<module>/src/playerbot [-DCORE_SOURCE_DIR=<module>/../..] (core premises only when given)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A17: missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A17: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A17: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A17: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A17: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#541 A17: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "#541 A17: PB_SOURCE_DIR is required")
endif()

read_source("${PB_SOURCE_DIR}/PlayerbotAIBase.cpp" base_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAIBase.h" base_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_source("${PB_SOURCE_DIR}/CombatIdlePolicy.h" policy_h)
read_source("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rnd_mgr)

# Switch: default off, documented with 0.
require_text("${config_h}" "bool perfCombatIdleYield = false;" "member default off")
require_text("${config_cpp}" "perfCombatIdleYield = config.GetBoolDefault(\"AiPlayerbot.Perf.CombatIdleYield\", false);" "config default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.CombatIdleYield = 0\n" "documented key with value 0")
reject_text("${conf_dist}" "\nAiPlayerbot.Perf.CombatIdleYield = 1" "shipped value 1")

# The shared yield of PlayerbotAIBase is untouched (PlayerbotMgr and other AIs, switch off).
function_body("${base_cpp}" "void PlayerbotAIBase::YieldAIInternalThread(bool minimal)" yield_body)
require_text("${yield_body}" "aiInternalUpdateDelay = minimal ? sPlayerbotAIConfig.reactDelay * 10 : sPlayerbotAIConfig.reactDelay;" "base yield unchanged")
reject_text("${base_cpp}" "combat_idle" "A17 code in PlayerbotAIBase")
require_text("${base_h}" "bool CanUpdateAIInternal() const { return aiInternalUpdateDelay < 100U; }" "CanUpdateAIInternal premise (< 100)")

# Policy: factor 3, atomics only, no shared containers.
require_text("${policy_h}" "constexpr std::uint32_t ReactFactor = 3;" "factor 3")
require_text("${policy_h}" "return switchOn && combatIdleTick && yieldSetDelay && !minimalYield;" "MayStretch gate")
require_text("${policy_h}" "return pendingVictim != 0 && (victim != pendingVictim || !(meleeSwing || autoRepeat));" "ShouldWake rule")
require_text("${policy_h}" "return currentDelay != 0 && currentDelay <= stretchedDelay;" "WakeResets rule")
require_text("${policy_h}" "inline std::atomic<std::uint64_t> stretched{ 0 };" "atomic counter")
require_text("${policy_h}" "inline std::atomic<std::uint64_t> woken{ 0 };" "atomic counter")
reject_text("${policy_h}" "std::map" "shared container")
reject_text("${policy_h}" "std::unordered_map" "shared container")
reject_text("${policy_h}" "std::vector" "shared container")
reject_text("${policy_h}" "shared_mutex" "shared_mutex")

# UpdateAI: wake after the countdown and before the internal update; pass clears the pending wait before the
# cast-time return; stretch gated, after the yield, only when the yield set the delay.
function_body("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" update_body)
require_order("${update_body}" "aiInternalUpdateDelay = 0;\n        isWaiting = false;\n    }" "if (!combatIdleVictim.IsEmpty())" "wake check after the countdown")
require_order("${update_body}" "if (!combatIdleVictim.IsEmpty())" "if(!UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()) && CanUpdateAIInternal())" "wake check before the internal update")
require_text("${update_body}" "bool const meleeSwing = victim && bot->hasUnitState(UNIT_STAT_MELEE_ATTACKING) && bot->CanReachWithMeleeAutoAttack(victim);\n        if (ai::combat_idle::ShouldWake(combatIdleVictim.GetRawValue(), victim ? victim->GetObjectGuid().GetRawValue() : 0,\n            meleeSwing, bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) != nullptr))" "wake call with victim, melee reach and autorepeat")
require_text("${update_body}" "if (ai::combat_idle::WakeResets(aiInternalUpdateDelay, ai::combat_idle::StretchedDelay(sPlayerbotAIConfig.reactDelay)))\n            {\n                ResetAIInternalUpdateDelay();\n                ai::combat_idle::woken.fetch_add(1, std::memory_order_relaxed);" "wake cuts only up to the stretched wait")
require_order("${update_body}" "CanUpdateAIInternal())\n    {\n        // twow-repo#541 (audit A17): this pass ends any stretched combat-idle wait.\n        combatIdleVictim.Clear();" "// Update the delay with the spell cast time" "pending wait cleared before the cast-time return")
require_text("${update_body}" "combatIdleTick = false;\n        UpdateAIInternal(elapsed, minimal);" "idle flag reset per pass")
require_text("${update_body}" "bool const yieldSetDelay = aiInternalUpdateDelay < sPlayerbotAIConfig.reactDelay;\n        YieldAIInternalThread(min);" "yield condition captured right before the yield")
require_order("${update_body}" "YieldAIInternalThread(min);" "if (ai::combat_idle::MayStretch(sPlayerbotAIConfig.perfCombatIdleYield, combatIdleTick, yieldSetDelay, min) && !HasRealPlayerMaster())\n            StretchCombatIdle();" "stretch after the yield, gated by the switch and the real master")

# Exactly one stretch call site (string(FIND) loop: a regex match list would split at the ';').
require_count("${ai_cpp}" "StretchCombatIdle();" 1 "StretchCombatIdle() call sites")

# DoNextAction: engine result feeds the idle flag, combat engine only, one engine call.
function_body("${ai_cpp}" "void PlayerbotAI::DoNextAction(bool min)" next_body)
require_text("${next_body}" "Engine* const engine = currentEngine;\n    bool const executed = engine->DoNextAction(NULL, 0, (minimal || min), bot->IsTaxiFlying());" "engine result captured")
require_text("${next_body}" "combatIdleTick = !executed && engine == engines[(uint8)BotState::BOT_STATE_COMBAT] && currentEngine == engine;" "combat engine only")
reject_text("${next_body}" "currentEngine->DoNextAction(" "second or uncaptured engine call")

# StretchCombatIdle: own bot only, exact delay without jitter, heal strategies and casting excluded, melee only in reach.
function_body("${ai_cpp}" "void PlayerbotAI::StretchCombatIdle()" stretch_body)
require_text("${stretch_body}" "if (currentEngine != engines[(uint8)BotState::BOT_STATE_COMBAT])\n        return;" "combat engine re-checked at the stretch")
require_text("${stretch_body}" "if (!victim || bot->IsNonMeleeSpellCasted(true, false, true))\n        return;" "no stretch while casting or channeling")
require_text("${stretch_body}" "bool const meleeSwing = bot->hasUnitState(UNIT_STAT_MELEE_ATTACKING) && bot->CanReachWithMeleeAutoAttack(victim);" "melee state only counts in reach")
require_text("${stretch_body}" "bool const autoRepeat = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) != nullptr;" "autorepeat test")
require_text("${stretch_body}" "if (!ai::combat_idle::IsAutoAttacking(victimIsTarget, meleeSwing, autoRepeat))\n        return;" "auto-attack test")
require_text("${stretch_body}" "bool const victimIsTarget = victim == aiObjectContext->GetValue<Unit*>(\"current target\")->Get();" "victim is the current target")
require_order("${stretch_body}" "if (ContainsStrategy(STRATEGY_TYPE_HEAL))\n        return;" "aiObjectContext->GetValue<Unit*>(\"current target\")" "heal check before the current-target lookup")
require_order("${stretch_body}" "if (!ai::combat_idle::IsAutoAttacking(" "aiInternalUpdateDelay = ai::combat_idle::StretchedDelay(sPlayerbotAIConfig.reactDelay);" "checks before the delay")
require_text("${stretch_body}" "combatIdleVictim = victim->GetObjectGuid();" "pending victim recorded")
require_text("${stretch_body}" "ai::combat_idle::stretched.fetch_add(1, std::memory_order_relaxed);" "stretch counted")
reject_text("${stretch_body}" "SetAIInternalUpdateDelay(" "jitter path")
reject_text("${stretch_body}" "GetBotAI(" "cross-bot read")
reject_text("${stretch_body}" "static " "static state")
require_text("${ai_h}" "void StretchCombatIdle();" "declaration")
require_text("${ai_h}" "bool combatIdleTick = false;" "member")
require_text("${ai_h}" "ObjectGuid combatIdleVictim;" "member")

# [CombatIdle] minute line: gated, world thread, plain call before the PerfMon Init.
function_body("${rnd_mgr}" "void ReportCombatIdle()" report_body)
require_text("${report_body}" "if (!sPlayerbotAIConfig.perfCombatIdleYield || now < lastReport + 60)\n        return;" "minute line gated")
require_text("${report_body}" "\"[CombatIdle] stretched=%llu woken=%llu\"" "minute line format")
function_body("${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd_body)
require_order("${upd_body}" "\n    ReportBotUpdate();" "\n    ReportCombatIdle();" "report after [BotUpdate]")
require_order("${upd_body}" "\n    ReportCombatIdle();" "sPerformanceMonitor.Init(0, 0);" "report before the PerfMon Init")

# Core premises (only when the core tree is given).
if(DEFINED CORE_SOURCE_DIR AND EXISTS "${CORE_SOURCE_DIR}/src/game/Objects/Unit.h")
  read_source("${CORE_SOURCE_DIR}/src/game/Objects/Unit.h" unit_h)
  read_source("${CORE_SOURCE_DIR}/src/game/Objects/Object.h" object_h)
  read_source("${CORE_SOURCE_DIR}/src/game/Objects/UnitDefines.h" unit_defines_h)
  require_text("${unit_h}" "bool CanReachWithMeleeAutoAttack(Unit const* pVictim, float flat_mod = 0.0f) const;" "core melee reach test")
  require_text("${unit_h}" "Unit* GetVictim() const { return m_attacking; }" "core victim accessor")
  require_text("${object_h}" "bool IsNonMeleeSpellCasted(bool withDelayed = false, bool skipChanneled = false, bool skipAutorepeat = false) const;" "core casting test")
  require_text("${unit_defines_h}" "UNIT_STAT_MELEE_ATTACKING = 0x00000001," "core melee state")
endif()

message(STATUS "combat_idle_yield source contract passed")
