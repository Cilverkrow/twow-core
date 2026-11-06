# twow-repo#602 ROT-01 W24 duel hang (OB-00 06.11.2026, HARM per ADR-0041 R4): AiPlayerbot.Rotation.DuelRule, code default 1 (0 = rollback).
# Two bots of one group stood in a duel for the rest of the run (11 pairs in 6 runs, 190-640 min each): "rpg duel" challenged a group
# member, the challenger's own "accept duel" is a no-op in the core, "duel no attackers" waited for "my attacker count" > 0 (nobody hit
# first), and the core never ends a duel by time. This contract pins the dist keys, the config members and reads (unknown modes = the
# default 1), the parsed values in [ConfigEcho] (owner rule #615, sorted #710), the pure policy, the four wirings (group skip in
# RpgDuelTrigger, initiator skip + the decline return in AcceptDuelAction, engage in DuelNoAttackersTrigger with the old rule first, the
# "duel stalled" -> "cancel duel" node) and the [DuelRule] minute line. The rules are tested in t/duel_rule_policy_tests.cpp.

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
    message(FATAL_ERROR "#602 DuelRule: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#602 DuelRule: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The body of a function: from its signature to the first line that is only "}" (column 0).
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "#602 DuelRule: not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("DuelRulePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/triggers/RpgTriggers.cpp" rpg_cpp)
read_source("strategy/triggers/GenericTriggers.cpp" triggers_cpp)
read_source("strategy/triggers/GenericTriggers.h" triggers_h)
read_source("strategy/triggers/TriggerContext.h" trigger_ctx)
read_source("strategy/actions/AcceptDuelAction.h" accept_h)
read_source("strategy/actions/WorldPacketActionContext.h" action_ctx)
read_source("strategy/generic/DuelStrategy.cpp" strategy_cpp)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; CancelSeconds 60; [ConfigEcho], sorted.
require_text("${dist}" "BEHAVIOUR CHANGE, ON by default, 0 = rollback; HARM per ADR-0041 R4" "dist text: on by default, rollback 0")
require_text("${dist}" "\nAiPlayerbot.Rotation.DuelRule = 1\n" "dist key, default 1")
require_text("${dist}" "\nAiPlayerbot.Rotation.DuelRule.CancelSeconds = 60\n" "dist cancel seconds, default 60")
require_text("${config_h}" "uint32 rotationDuelRule = 1;" "config member, default 1")
require_text("${config_h}" "uint32 rotationDuelRuleCancelSeconds = 60;" "config member cancel seconds, default 60")
require_text("${config_cpp}" "rotationDuelRule = config.GetIntDefault(\"AiPlayerbot.Rotation.DuelRule\", 1);   // twow-repo#602 W24 duel hang\n    if (rotationDuelRule > 2)\n        rotationDuelRule = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "rotationDuelRuleCancelSeconds = config.GetIntDefault(\"AiPlayerbot.Rotation.DuelRule.CancelSeconds\", 60);" "config read cancel seconds, default 60")
require_text("${config_cpp}" "            {\"Rotation.DarkRuneGate\", Value(rotationDarkRuneGate)},\n            {\"Rotation.DuelRule\", Value(rotationDuelRule)},\n            {\"Rotation.DuelRule.CancelSeconds\", Value(rotationDuelRuleCancelSeconds)},\n            {\"Rotation.EmergencyLadder\", Value(rotationEmergencyLadder)},\n" "parsed values in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only) and its rules.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 DuelRule: DuelRulePolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "return applies && sameGroup;" "group duel skip")
require_text("${policy}" "return applies && isInitiator;" "initiator accept skip")
require_text("${policy}" "return applies && duelStarted && !duelFinished && !targetIsOpponent;" "engage only a running duel")
require_text("${policy}" "    if (!inDuel || fighting)\n        return 0;\n    return quietSince ? quietSince : now;" "the quiet clock")
require_text("${policy}" "return applies && opponentIsBot && cancelSeconds > 0 && quietSince > 0 && now >= quietSince + cancelSeconds;" "cancel only bot-vs-bot after cancelSeconds")

# 3. RpgDuelTrigger: the group skip after the player lookup, before the level checks.
function_body("${rpg_cpp}" "bool RpgDuelTrigger::IsActive()" rpg_body)
require_text("${rpg_cpp}" "#include \"playerbot/DuelRulePolicy.h\"" "the policy include in RpgTriggers.cpp")
require_text("${rpg_body}" "    if (ai::duel_rule::SkipGroupDuel(ai::duel_rule::Applies(sPlayerbotAIConfig.rotationDuelRule, bot->GetGUIDLow()), bot->IsInGroup(player)))\n    {\n        ai::duel_rule::skippedGroup.fetch_add(1, std::memory_order_relaxed);\n        return false;\n    }\n" "the group skip")
require_order("${rpg_body}" "    if (!player)\n        return false;" "ai::duel_rule::SkipGroupDuel(" "the group skip after the player lookup")
require_order("${rpg_body}" "ai::duel_rule::SkipGroupDuel(" "if (player->GetLevel() > bot->GetLevel() + 3)" "the group skip before the level checks")

# 4. AcceptDuelAction: the initiator skip first; the decline returns (with the switch) before the accept.
require_text("${accept_h}" "#include \"playerbot/DuelRulePolicy.h\"" "the policy include in AcceptDuelAction.h")
require_text("${accept_h}" "            bool const duelRule = ai::duel_rule::Applies(sPlayerbotAIConfig.rotationDuelRule, bot->GetGUIDLow());\n            if (ai::duel_rule::SkipInitiatorAccept(duelRule, bot->m_duel && bot->m_duel->initiator == bot))\n            {\n                ai::duel_rule::skippedAccept.fetch_add(1, std::memory_order_relaxed);\n                return false;\n            }\n" "the initiator skip")
require_text("${accept_h}" "                bot->GetSession()->HandleDuelCancelledOpcode(packet);\n\n                // twow-repo#602 W24 (DuelRule): a declined duel stops here; before, the accept below was sent after the cancel.\n                if (duelRule)\n                    return true;\n            }\n\n            WorldPacket packet(CMSG_DUEL_ACCEPTED, 8);" "the decline returns before the accept")
require_order("${accept_h}" "ai::duel_rule::SkipInitiatorAccept(" "// do not auto duel with low hp or below certain level" "the initiator skip before the decline")

# 5. DuelNoAttackersTrigger: m_duel first, the old rule unchanged, then the engage of a running duel.
function_body("${triggers_cpp}" "bool DuelNoAttackersTrigger::IsActive()" engage_body)
require_text("${triggers_cpp}" "#include \"playerbot/DuelRulePolicy.h\"" "the policy include in GenericTriggers.cpp")
require_text("${engage_body}" "    if (!bot->m_duel)\n        return false;\n\n    if (NoAttackersTrigger::IsActive())\n        return true;\n" "m_duel first, then the old rule")
require_text("${engage_body}" "    Unit* const opponent = AI_VALUE(Unit*, \"duel target\");\n    Unit* const current = AI_VALUE(Unit*, \"current target\");\n    if (opponent && ai::duel_rule::EngageOpponent(ai::duel_rule::Applies(sPlayerbotAIConfig.rotationDuelRule, bot->GetGUIDLow()),\n            bot->m_duel->startTime != 0, bot->m_duel->finished, current == opponent))" "engage a running duel (startTime set)")
require_order("${engage_body}" "NoAttackersTrigger::IsActive()" "ai::duel_rule::EngageOpponent(" "the old rule before the engage")

# 6. "duel stalled" -> "cancel duel": trigger (one clock per bot), the cancel, registrations, the strategy node above "select new target".
function_body("${triggers_cpp}" "bool DuelStalledTrigger::IsActive()" stalled_body)
require_text("${stalled_body}" "ObjectAccessor::GetUnit(*bot, bot->m_duel->opponent)" "the opponent on the bot's map")
require_text("${stalled_body}" "quietSince = ai::duel_rule::NextQuietSince(quietSince, opponent != nullptr, opponent && (bot->IsInCombat() || opponent->IsInCombat()), now);" "the quiet clock: neither duellist in combat")
require_text("${stalled_body}" "ai::duel_rule::CancelDue(applies, opponent->IsPlayer() && GetBotAI(static_cast<Player*>(opponent)) != nullptr, quietSince, now, sPlayerbotAIConfig.rotationDuelRuleCancelSeconds)" "cancel only bot-vs-bot")
require_text("${triggers_h}" "class DuelStalledTrigger : public Trigger" "the trigger class")
require_text("${triggers_h}" "uint64 quietSince = 0;" "the trigger's quiet clock")
require_text("${trigger_ctx}" "creators[\"duel stalled\"] = [](PlayerbotAI* ai) { return new DuelStalledTrigger(ai); };" "trigger registration")
require_text("${action_ctx}" "creators[\"cancel duel\"] = [](PlayerbotAI* ai) { return new CancelDuelAction(ai); };" "action registration")
require_text("${accept_h}" "class CancelDuelAction : public Action" "the cancel action")
require_text("${accept_h}" "            WorldPacket packet(CMSG_DUEL_CANCELLED, 8);\n            packet << bot->GetGuidValue(PLAYER_DUEL_ARBITER);\n            bot->GetSession()->HandleDuelCancelledOpcode(packet);\n            ai::duel_rule::cancelled.fetch_add(1, std::memory_order_relaxed);\n            return true;" "the cancel sends CMSG_DUEL_CANCELLED and counts")
require_text("${strategy_cpp}" "        \"duel stalled\",\n        NextAction::array(0, new NextAction(\"cancel duel\", 95.0f), NULL)));" "the strategy node, 95 above select new target 90")
require_text("${strategy_cpp}" "        \"duel no attackers\",\n        NextAction::array(0, new NextAction(\"attack duel opponent\", 70.0f), NULL)));" "the engage node unchanged")

# 7. The [DuelRule] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/DuelRulePolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
function_body("${mgr_cpp}" "void ReportDuelRule()" report_body)
require_text("${report_body}" "    if (!sPlayerbotAIConfig.rotationDuelRule || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${report_body}" "sLog.outBasic(\"[DuelRule] mode=%u cancel_s=%u skipped_group=%llu skipped_accept=%llu engaged=%llu cancelled=%llu\", sPlayerbotAIConfig.rotationDuelRule,\n        sPlayerbotAIConfig.rotationDuelRuleCancelSeconds,\n        (unsigned long long)ai::duel_rule::skippedGroup.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::duel_rule::skippedAccept.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::duel_rule::engaged.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::duel_rule::cancelled.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportDuelRule();" "the minute report is called")

message(STATUS "#602 DuelRule source contract passed")
