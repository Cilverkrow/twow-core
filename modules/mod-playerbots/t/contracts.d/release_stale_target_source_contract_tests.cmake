# A3-HARM-UPSTREAM H6 (T40) (AiPlayerbot.Rotation.ReleaseStaleTarget, default 0 = the old rule; idea only: cmangos/playerbots 21b8aef2 +
# f6f1d3f). A bot out of combat that keeps a valid current target (pull path return before "attack target", ChooseTargetActions.cpp
# :165-174 before :179; an "attack target" reset by travel) and stands still drops ONLY that current target, so the grind strategy's
# "no target" -> "attack anything" re-picks. This contract pins the dist keys, the config members and reads (unknown modes = 0), the
# parsed values in [ConfigEcho] (sorted, #710), the pure policy and its named constants, the wiring in NoTargetTrigger (the old rule
# first with the same short-circuit, the mode-0 return before any new read, the one write "current target" = nullptr, nothing else
# written or reset - no movement, position, travel, rpg, PosStuck, attack / pull / old target, AttackStop or bot->Attack), the per-bot
# clock members and the [StaleTarget] minute line. The rules are tested in t/stale_target_policy_tests.cpp.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "H6 ReleaseStaleTarget: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "H6 ReleaseStaleTarget: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "H6 ReleaseStaleTarget: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# How often needle occurs in text (string(FIND) loop - no MATCHALL, so a ";" in the needle is safe).
function(count_text text needle out_var)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR from "${at} + ${len}")
    string(SUBSTRING "${rest}" ${from} -1 rest)
  endwhile()
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

# The body of a function: from its signature to the first line that is only "}" (column 0).
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "H6 ReleaseStaleTarget: not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("StaleTargetPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/triggers/GenericTriggers.cpp" triggers_cpp)
read_source("strategy/triggers/GenericTriggers.h" triggers_h)
read_source("strategy/triggers/TriggerContext.h" trigger_ctx)
read_source("strategy/generic/GrindingStrategy.cpp" grinding_cpp)

# 1. The switch: default 0 in the dist, members and reads; unknown modes are 0; [ConfigEcho], sorted between ReachPlayerMaster and RestGate.
require_text("${dist}" "BEHAVIOUR CHANGE, default 0 = as before" "dist text: default 0 = as before")
require_text("${dist}" "\nAiPlayerbot.Rotation.ReleaseStaleTarget = 0\n" "dist key, default 0")
require_text("${dist}" "\nAiPlayerbot.Rotation.ReleaseStaleTarget.Shadow = 0\n" "dist shadow, default 0")
require_text("${dist}" "\nAiPlayerbot.Rotation.ReleaseStaleTarget.StillSeconds = 60\n" "dist still seconds, default 60")
require_text("${dist}" "\nAiPlayerbot.Rotation.ReleaseStaleTarget.StoppedSeconds = 5\n" "dist stopped seconds, default 5")
require_text("${dist}" "# 0 = as before (default), 1 = every bot, 2 = only cohort B (odd low guid). Any other value acts like 0.\nAiPlayerbot.Rotation.ReleaseStaleTarget = 0\n" "dist modes, unknown = 0")
require_text("${config_h}" "uint32 rotationReleaseStaleTarget = 0;" "config member, default 0")
require_text("${config_h}" "uint32 rotationReleaseStaleTargetShadow = 0;" "config member shadow, default 0")
require_text("${config_h}" "uint32 rotationReleaseStaleTargetStillSeconds = 60;" "config member still seconds, default 60")
require_text("${config_h}" "uint32 rotationReleaseStaleTargetStoppedSeconds = 5;" "config member stopped seconds, default 5")
require_text("${config_cpp}" "rotationReleaseStaleTarget = config.GetIntDefault(\"AiPlayerbot.Rotation.ReleaseStaleTarget\", 0);   // A3-HARM-UPSTREAM H6 (T40)\n    if (rotationReleaseStaleTarget > 2)\n        rotationReleaseStaleTarget = 0;" "config read, > 2 = 0")
require_text("${config_cpp}" "rotationReleaseStaleTargetShadow = config.GetIntDefault(\"AiPlayerbot.Rotation.ReleaseStaleTarget.Shadow\", 0);\n    if (rotationReleaseStaleTargetShadow > 1)\n        rotationReleaseStaleTargetShadow = 0;" "config read shadow, > 1 = 0")
require_text("${config_cpp}" "rotationReleaseStaleTargetStillSeconds = config.GetIntDefault(\"AiPlayerbot.Rotation.ReleaseStaleTarget.StillSeconds\", 60);" "config read still seconds")
require_text("${config_cpp}" "rotationReleaseStaleTargetStoppedSeconds = config.GetIntDefault(\"AiPlayerbot.Rotation.ReleaseStaleTarget.StoppedSeconds\", 5);" "config read stopped seconds")
require_text("${config_cpp}" "            {\"Rotation.ReachPlayerMaster\", Value(rotationReachPlayerMaster)},\n            {\"Rotation.ReleaseStaleTarget\", Value(rotationReleaseStaleTarget)},\n            {\"Rotation.ReleaseStaleTarget.Shadow\", Value(rotationReleaseStaleTargetShadow)},\n            {\"Rotation.ReleaseStaleTarget.StillSeconds\", Value(rotationReleaseStaleTargetStillSeconds)},\n            {\"Rotation.ReleaseStaleTarget.StoppedSeconds\", Value(rotationReleaseStaleTargetStoppedSeconds)},\n            {\"Rotation.RestGate\", Value(rotationRestGate)},\n" "parsed values in [ConfigEcho], sorted (#710)")

# 2. Pure policy (std + the cohort rule only), the named constants and the core rules.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "H6 ReleaseStaleTarget: StaleTargetPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "constexpr std::uint32_t DefaultStillSeconds = 60;" "named constant StillSeconds")
require_text("${policy}" "constexpr std::uint32_t DefaultStoppedSeconds = 5;" "named constant StoppedSeconds")
require_text("${policy}" "constexpr std::uint32_t GapMs = 5000;" "named constant GapMs")
require_text("${policy}" "constexpr float MoveYards = 5.0f;" "named constant MoveYards")
require_text("${policy}" "constexpr std::uint32_t FollowWindowMs = 30000;" "named constant FollowWindowMs")
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "return shadow == 1 && mode != 0 && !Applies(mode, guidLow);" "shadow = the control cohort only")
require_text("${policy}" "return inCombat || !alive || realPlayer || taxi || transport || duel || battleground;" "the Reset cases")
require_text("${policy}" "return applies && due != Due::None && hold == Hold::None;" "release only applies + due + no hold")
require_text("${policy}" "    else if (c.mapId != mapId || std::uint32_t(nowMs - c.lastSeenMs) > GapMs || Dist2(x, y, z, c.x, c.y, c.z) >= MoveYards * MoveYards)" "gap / map / move restart both clocks")

# 3. NoTargetTrigger::IsActive: the old rule first (same reads, same short-circuit), then the mode-0 return, then the new check only.
function_body("${triggers_cpp}" "bool NoTargetTrigger::IsActive()" active_body)
require_text("${triggers_cpp}" "#include \"playerbot/StaleTargetPolicy.h\"" "the policy include in GenericTriggers.cpp")
require_text("${active_body}" "{\n    // The old rule first, the same reads in the same short-circuit order.\n    Unit* const target = AI_VALUE(Unit*, \"current target\");\n    bool const oldRule = !target || AI_VALUE2(bool, \"invalid target\", \"current target\");\n    // A3-HARM-UPSTREAM H6 (T40) (AiPlayerbot.Rotation.ReleaseStaleTarget, default 0): 0 = the old rule, nothing else is read.\n    uint32 const mode = sPlayerbotAIConfig.rotationReleaseStaleTarget;\n    if (!mode)\n        return oldRule;\n\n    return StaleTargetCheck(target, oldRule, mode);" "IsActive: old rule, mode-0 return, then the check")
require_order("${active_body}" "AI_VALUE2(bool, \"invalid target\", \"current target\")" "if (!mode)\n        return oldRule;" "the old rule before the mode check")
require_order("${active_body}" "if (!mode)\n        return oldRule;" "StaleTargetCheck(" "the mode-0 return before the new check")

# 4. StaleTargetCheck: early return for a bot without a role, Reset before the clock, holds only when due, one write.
function_body("${triggers_cpp}" "bool NoTargetTrigger::StaleTargetCheck(Unit* target, bool oldRule, uint32 mode)" check_body)
require_order("${check_body}" "    if (!applies && !shadow)\n        return oldRule;" "WorldTimer::getMSTime()" "a bot neither acting nor shadow reads nothing else")
require_order("${check_body}" "ai::stale_target::IsReset(" "ai::stale_target::Observe(" "Reset before the clock")
require_order("${check_body}" "ai::stale_target::DueOf(" "PullStrategy::Get(ai)" "the holds are read only after due")
require_order("${check_body}" "    if (due == ai::stale_target::Due::None)\n        return false;" "WaitForAttackStrategy::ShouldWait(ai)" "the wait hold only when due")
require_order("${check_body}" "    if (shadow)\n    {" "SET_AI_VALUE(Unit*, \"current target\", nullptr);" "the shadow returns before the write")
require_text("${check_body}" "!bot->GetLootGuid().IsEmpty()" "loot hold = an open loot window only")
require_text("${check_body}" "ai::stale_target::IsReset(bot->IsInCombat(), sServerFacade.IsAlive(bot), ai->HasRealPlayerMaster(), bot->IsTaxiFlying(),\n            bot->GetTransport() != nullptr, bot->m_duel != nullptr, bot->InBattleGround()))" "the whole IsReset call, every input (v1.2 F2)")
# Rest = sitting or a food / drink aura (AURA_INTERRUPT_FLAG_NOT_SEATED); a bare regen aura type (Blessing of Wisdom, mp5 gear) is not Rest (v1.2 F1).
require_text("${check_body}" "bot->IsSitState() || StaleTargetEatOrDrink(bot));" "rest hold: sitting, eating, drinking")
forbid_text("${check_body}" "HasAuraType(SPELL_AURA_MOD_REGEN)" "rest hold on any health regen aura")
forbid_text("${check_body}" "HasAuraType(SPELL_AURA_MOD_POWER_REGEN)" "rest hold on any power regen aura (Blessing of Wisdom, mp5 gear)")
function_body("${triggers_cpp}" "static bool StaleTargetEatOrDrink(Player* bot)" eat_body)
require_text("${eat_body}" "    for (Aura const* aura : bot->GetAurasByType(SPELL_AURA_MOD_REGEN))\n        if (ai::stale_target::IsEatOrDrinkAura(aura->GetSpellProto()->AuraInterruptFlags))\n            return true;\n    for (Aura const* aura : bot->GetAurasByType(SPELL_AURA_MOD_POWER_REGEN))\n        if (ai::stale_target::IsEatOrDrinkAura(aura->GetSpellProto()->AuraInterruptFlags))\n            return true;\n    return false;" "the food / drink aura filter")
require_order("${triggers_cpp}" "static bool StaleTargetEatOrDrink(Player* bot)" "bool NoTargetTrigger::StaleTargetCheck(" "the filter defined before its use")
require_text("${policy}" "constexpr std::uint32_t NotSeatedInterruptFlag = 0x00040000u;" "named constant NotSeatedInterruptFlag")
require_text("${policy}" "return (auraInterruptFlags & NotSeatedInterruptFlag) != 0;" "food / drink = NOT_SEATED")
file(READ "${CORE_SOURCE_DIR}/src/game/Spells/SpellDefines.h" core_spell_defines)
require_text("${core_spell_defines}" "AURA_INTERRUPT_FLAG_NOT_SEATED                  = 0x00040000," "the core value of AURA_INTERRUPT_FLAG_NOT_SEATED")
require_text("${check_body}" "ai->HasRealPlayerMaster()" "real player master is Reset")
count_text("${check_body}" "AI_VALUE" n_ai_value)
if(NOT n_ai_value EQUAL 1)
  message(FATAL_ERROR "H6 ReleaseStaleTarget: StaleTargetCheck must use exactly one *AI_VALUE (the write), found ${n_ai_value}")
endif()
count_text("${check_body}" "SET_AI_VALUE(Unit*, \"current target\", nullptr);" n_write)
if(NOT n_write EQUAL 1)
  message(FATAL_ERROR "H6 ReleaseStaleTarget: exactly one write of \"current target\" = nullptr expected, found ${n_write}")
endif()
set(body_all "${active_body}\n${check_body}")
foreach(bad
    "LastMovement" "\"last movement\"" "\"current position\"" "\"travel target\"" "\"rpg target\"" "PosStuck" "\"attack target\""
    "\"pull target\"" "\"old target\"" "AttackStop" "bot->Attack(" "->Attack(" "RESET_AI_VALUE" "SetSelectionGuid" "StopMoving"
    "ClearValues" "TravelTarget" "\"has available loot\"" "unreachable" "OnPullEnded" "SetTarget(")
  forbid_text("${body_all}" "${bad}" "inside the H6 code")
endforeach()

# 5. The per-bot clock members inside class NoTargetTrigger; the registration and the strategy node unchanged.
require_text("${triggers_h}" "#include \"playerbot/StaleTargetPolicy.h\"" "the policy include in GenericTriggers.h")
function_body("${triggers_h}" "    class NoTargetTrigger : public Trigger" no_target_class)
string(FIND "${no_target_class}" "    };" class_end)
string(SUBSTRING "${no_target_class}" 0 ${class_end} no_target_class)
require_text("${no_target_class}" "ai::stale_target::Clock staleClock;" "the clock member")
require_text("${no_target_class}" "ai::stale_target::FollowUp staleFollowUp;" "the follow-up member")
require_text("${no_target_class}" "ai::stale_target::MinuteMark staleEligMinute;" "the minute member")
require_text("${trigger_ctx}" "creators[\"no target\"] = [](PlayerbotAI* ai) { return new NoTargetTrigger(ai); };" "trigger registration unchanged")
require_text("${grinding_cpp}" "        \"no target\",\n        NextAction::array(0,\n        new NextAction(\"attack anything\", 5.0f), NULL)));" "the strategy node unchanged")

# 6. The [StaleTarget] minute line: nothing when mode and shadow are 0, increments (exchange), A/B pairs, called after [DuelRule].
require_text("${mgr_cpp}" "#include \"playerbot/StaleTargetPolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
function_body("${mgr_cpp}" "static unsigned long long TakeStaleTarget(std::atomic<std::uint64_t>& counter)" take_body)
require_text("${take_body}" "return (unsigned long long)counter.exchange(0, std::memory_order_relaxed);" "counters as increments")
function_body("${mgr_cpp}" "void ReportStaleTarget()" report_body)
require_text("${report_body}" "    if ((!sPlayerbotAIConfig.rotationReleaseStaleTarget && !sPlayerbotAIConfig.rotationReleaseStaleTargetShadow) || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${report_body}" "sLog.outBasic(\"[StaleTarget] mode=%u shadow=%u still_s=%u stopped_s=%u elig_bot_min=%llu/%llu released=%llu/%llu by_still=%llu/%llu by_stopped=%llu/%llu shadow_would=%llu/%llu re_same=%llu/%llu moved_on=%llu/%llu held_pull=%llu/%llu held_wait=%llu/%llu held_loot=%llu/%llu held_cast=%llu/%llu held_rest=%llu/%llu\"," "the minute line format")
count_text("${report_body}" "TakeStaleTarget(" n_take)
if(NOT n_take EQUAL 24)
  message(FATAL_ERROR "H6 ReleaseStaleTarget: 12 A/B pairs (24 TakeStaleTarget) expected, found ${n_take}")
endif()
require_text("${mgr_cpp}" "    ReportQuestBook();      // A3 step 2: [QuestBookMin] once per minute (AiPlayerbot.QuestBook / QuestBook.Shadow)\n    ReportStaleTarget();" "the minute report right after [QuestBookMin] (QB-K pins DuelRule -> QuestBook adjacent)")

message(STATUS "H6 ReleaseStaleTarget source contract passed")
