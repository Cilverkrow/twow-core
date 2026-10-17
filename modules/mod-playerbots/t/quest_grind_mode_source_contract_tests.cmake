cmake_policy(VERSION 3.16)
# twow-repo#601 GrindMode (owner decision 17.10.2026 via OB-00, strict: "lieber herumstehen und nichts tun als grinden").
# AiPlayerbot.QuestFirstProgression.GrindMode, restart-only, default 0. Pins:
#  - the pure policy QuestGrindModePolicy.h (mode 1 == mode 0 until S4, population, targets, reasons, bounded log),
#  - every blocked grind path with GrindMode 2:
#      1 the 8.32 fallback NeedTravelPurposeValue(Grind)            -> reason fallback
#      2 the money grind / plain grind request (RequestTravelTarget) -> reason money (or fallback)
#      3 the opportunistic "attack anything" (grind target filter)   -> reason opportunistic
#      4 the "grind" strategy itself (its own multiplier)            -> reason opportunistic / no_target
#      5 WorldBuffTravelMultiplier keeps "attack anything" at 0
#      6 the Grind bit of the shared entry purpose map: a pure GrindTravelDestination is not active
#      7 ChooseTravelTargetAction: a grind fetch is never taken; quest objective mob fetches stay
#  - the positive exceptions (attackers / group help, quest objective kills, GenericRpg, Explore, gathering, RosterGuild),
#  - the [QuestIdle] lines (once per reason and idle episode per bot, minute counter on the world thread), only with
#    GrindMode 2,
#  - GrindMode 0 byte-identical branches, neutral default,
#  - the S1 recheck nit: the abandon mark on every autonomous accept path (dialog, packet, accept all, share, escort
#    confirm); the guild quest order is an officer's explicit order and stays without it.
# Reads only named files below modules/mod-playerbots/src/playerbot (never a glob, never src/modules/Eluna).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 GrindMode: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 GrindMode: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#601 GrindMode: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `start` up to the first `end` after it (both required).
function(region text start end out)
  string(FIND "${text}" "${start}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "#601 GrindMode: missing region start: ${start}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(LENGTH "${start}" start_length)
  string(SUBSTRING "${rest}" ${start_length} -1 tail)
  string(FIND "${tail}" "${end}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#601 GrindMode: missing region end: ${end}")
  endif()
  math(EXPR length "${start_length} + ${end_at}")
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
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#601 GrindMode: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("QuestGrindModePolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("TravelMgr.cpp" travel_mgr)
read_source("strategy/values/TravelValues.cpp" values)
read_source("strategy/values/GrindTargetValue.cpp" grind_target)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_target)
read_source("strategy/actions/AcceptQuestAction.cpp" accept)
read_source("strategy/actions/GuildAcceptQuestOrderAction.cpp" guild_quest_order)
read_source("strategy/generic/GrindingStrategy.h" grind_strategy_h)
read_source("strategy/generic/GrindingStrategy.cpp" grind_strategy)
read_source("strategy/generic/TravelStrategy.cpp" travel_strategy)
read_source("strategy/generic/WorldBuffTravelStrategy.cpp" wbuff)
read_source("strategy/generic/DpsAssistStrategy.cpp" dps_assist)
read_source("strategy/generic/TankAssistStrategy.cpp" tank_assist)
read_source("strategy/rogue/RogueStrategy.cpp" rogue)

# ---- 0. The policy is pure and outside the botpch.h chain -----------------------------------------
require_text("${policy}" "namespace ai::quest_grind_mode" "policy namespace")
string(REGEX MATCHALL "#include \"[^\"]+\"" policy_project_includes "${policy}")
if(policy_project_includes)
  message(FATAL_ERROR "#601 GrindMode: the policy includes no project header, found: ${policy_project_includes}")
endif()
foreach(banned "mutex" "std::atomic" "sPlayerbotAIConfig" "Player*" "sLog")
  forbid_text("${policy}" "${banned}" "game, lock or log state in the pure policy")
endforeach()
foreach(chain ai_h config_h)
  forbid_text("${${chain}}" "#include \"playerbot/QuestGrindModePolicy.h\"" "policy in the botpch.h chain (${chain})")
  forbid_text("${${chain}}" "#include \"QuestGrindModePolicy.h\"" "policy in the botpch.h chain (${chain})")
endforeach()
foreach(member "near" "far" "min" "max")
  forbid_text("${policy}" " ${member};" "member named ${member} (windows.h macro)")
  forbid_text("${policy}" " ${member} =" "member named ${member} (windows.h macro)")
endforeach()

# ---- 1. Modes: 0 = today, 1 = reserved (== 0 until S4), 2 = no grind -------------------------------
require_text("${policy}" "constexpr std::uint32_t ModeNoGrind = 2;" "mode 2 = no grind")
require_text("${policy}" "constexpr std::uint32_t ModeGapFiller = 1;   // reserved for S2 / S4, acts like ModeToday until then" "mode 1 reserved")
require_text("${policy}" "inline bool NoGrind(std::uint32_t grindMode)\n{\n    return grindMode == ModeNoGrind;\n}" "only mode 2 changes anything (1 == 0)")
count_text("${policy}" "ModeGapFiller" 1 "mode 1 has no rule of its own until S4")
foreach(src ai_cpp mgr travel_mgr values grind_target choose grind_strategy)
  forbid_text("${${src}}" "ModeGapFiller" "a mode-1 branch before S4 (${src})")
  forbid_text("${${src}}" "questFirstProgressionGrindMode == 1" "a mode-1 branch before S4 (${src})")
  forbid_text("${${src}}" "questFirstProgressionGrindMode == 2" "a mode test outside NoGrind() (${src})")
  forbid_text("${${src}}" "questFirstProgressionGrindMode != 0" "a mode test outside NoGrind() (${src})")
endforeach()

# ---- 2. Population: quest-first roster bots on their own below the max level -----------------------
require_text("${policy}" "return NoGrind(grindMode) && questFirst && !realMaster && (!maxLevel || level < maxLevel);" "population rule")
region("${ai_cpp}" "bool PlayerbotAI::IsQuestNoGrind()" "\n}\n" no_grind_fn)
require_order("${no_grind_fn}" "if (!ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode) || !bot)\n        return false;" "sRandomPlayerbotMgr.IsPersistentRosterMember" "GrindMode 0 / 1 return before any other test")
require_text("${no_grind_fn}" "sPlayerbotAIConfig.questFirstProgressionEnabled && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())," "the QuestFirstProgression population (UsesQuestFirstProgression)")
require_text("${no_grind_fn}" "HasRealPlayerMaster(), bot->GetLevel(), sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));" "on its own, below the max level")
require_text("${choose}" "bool UsesQuestFirstProgression(Player const* bot)\n{\n    return bot && sPlayerbotAIConfig.questFirstProgressionEnabled &&\n        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());\n}" "the gate IsQuestNoGrind mirrors")

# ---- 3. Path 1: the 8.32 fallback (NeedTravelPurposeValue Grind) -> fallback -----------------------
region("${values}" "    case TravelDestinationPurpose::Grind:\n    {" "        uint32 rpgPhase = ai->GetFixedBotNumber" grind_case)
require_order("${grind_case}" "if (UsesQuestFirstProgression(bot))" "if (ai->IsQuestNoGrind())" "inside the quest-first branch")
require_order("${grind_case}" "if (ai->IsQuestNoGrind())" "if (ai::quest_search::FreshBot(sPlayerbotAIConfig.directTargetsEnabled, bot->GetLevel(), sPlayerbotAIConfig.directTargetsFreshMaxLevel) &&" "before the fresh threshold")
require_order("${grind_case}" "if (ai->IsQuestNoGrind())" "return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures) &&" "before the unchanged 8.32 line")
region("${grind_case}" "if (ai->IsQuestNoGrind())" "            // twow-repo#594 / P3-a" fallback_block)
require_text("${fallback_block}" "ai->NoteQuestIdle(uint8(ai::quest_grind_mode::Reason::Fallback)," "reason fallback")
require_text("${fallback_block}" "if (ai::quest_search::GrindFallback(true, failures, minFailures) && !AI_VALUE2(bool, \"manual bool\", \"is travel refresh\"))" "logged only when the 8.32 rule would have let it grind")
require_text("${fallback_block}" "sPlayerbotAIConfig.directTargetsFreshGrindFailures : sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures;" "same threshold as the 8.32 rule (fresh included)")
require_text("${fallback_block}" "                return false;\n            }" "no grind fallback with GrindMode 2")
# Review fix (F5): skinning and boss trips are kill trips the grind target filter then refuses (dead trips with a
# misleading "opportunistic"), so with GrindMode 2 those two purposes are off. Mining, herbalism, fishing, GenericRpg and
# Explore stay: the skinning test names its purpose, the gather block is otherwise unchanged.
region("${values}" "    case TravelDestinationPurpose::GatherHerbalism:\n    {" "        // Hotfix 8.5: finished quests are handed in before the bot gathers again." gather_head)
require_text("${gather_head}" "        if (purpose == TravelDestinationPurpose::GatherSkinning && ai->IsQuestNoGrind())\n            return false;" "no skinning trip (a kill trip) with GrindMode 2; mining / herbalism / fishing stay")
region("${values}" "    case TravelDestinationPurpose::Boss:\n" "    case TravelDestinationPurpose::Mail:" boss_case)
require_text("${boss_case}" "        if (ai->IsQuestNoGrind())\n            return false;\n        return AI_VALUE(bool, \"can fight boss\");" "no boss trip (a kill trip) with GrindMode 2, as before otherwise")
count_text("${values}" "ai->IsQuestNoGrind()" 3 "the GrindMode tests in TravelValues: Grind fallback, skinning, boss (GenericRpg, Explore, mining, herbalism, fishing untouched)")
require_text("${conf}" "#       mob, boss, skinning) to a creature no active quest needs." "skinning / boss / forced kill trips documented")

# ---- 4. Path 2: the money grind and the plain grind request -> money / fallback ---------------------
require_text("${travel_strategy}" "{\"val::should get money\", TravelDestinationPurpose::Grind, 6.77f}," "money trigger unchanged (blocked at the request)")
require_text("${travel_strategy}" "{\"\",TravelDestinationPurpose::Grind, 6.35f}," "plain grind trigger unchanged (blocked by path 1 and at the request)")
region("${choose}" "bool RequestTravelTargetAction::isUseful() {" "\n}\n" request_useful)
require_order("${request_useful}" "if (!AI_VALUE(bool, \"can move around\"))\n        return false;" "if (ai->IsQuestNoGrind() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)))" "after the existing early returns")
require_order("${request_useful}" "if (ai->IsQuestNoGrind() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)))" "if (!isAllowed())" "before the skip chance (no urand drawn)")
require_text("${request_useful}" "ai->NoteQuestIdle(uint8(ai::quest_grind_mode::RequestReason(AI_VALUE(bool, \"should get money\")))," "reason money (or fallback)")
require_text("${request_useful}" "ai::quest_grind_mode::TargetText(\"request_travel_grind\", 0));\n        return false;" "the grind request is refused")
require_text("${policy}" "return condition.find(\"should get money\") != std::string::npos;" "the money trigger")
require_text("${policy}" "return moneyCondition ? Reason::Money : Reason::Fallback;" "money or fallback")

# ---- 5. Path 7: ChooseTravelTargetAction ------------------------------------------------------------
region("${choose}" "else if (futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)) && UsesQuestFirstProgression(bot))\n    {" "    setNewTarget(requester, &newTarget, travelTarget);" grind_branch)
require_order("${grind_branch}" "if (ai->IsQuestNoGrind())" "[QuestSearch] state=grind_fallback bot=%u level=%u failures=%d" "a grind fetch is refused before the grind_fallback line")
require_text("${grind_branch}" "AI_VALUE2(std::string, \"manual string\", \"future travel condition\")))),\n                ai::quest_grind_mode::TargetText(\"grind_destination\", grindEntry > 0 ? uint32(grindEntry) : 0u));\n            return false;" "never set as target, reason by the request condition")
# isAllowed and the quest objective mob fetches stay as they are (old :1574, :1794, :1900).
require_text("${choose}" "    case TravelDestinationPurpose::Grind:\n        return true;" "isAllowed Grind unchanged (the refusal is in isUseful)")
require_text("${choose}" "subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,\n                                (uint32)TravelDestinationPurpose::Grind, objectiveEntries, false, 1000000.0f);" "guild-order QuestReward objective mobs fetched as before")
require_text("${choose}" "uint32 mobPurpose = (uint32)TravelDestinationPurpose::Grind;" "guild-order drop mobs fetched as before (SetBestTarget drops the forced kill trips)")
# Review fix (F2): the guild order target is forced, and a forced target never asks IsActive (SetBestTarget,
# TravelTarget::CheckStatus, no status-time expiry) - the earlier "IsActive decides" was wrong. SetBestTarget drops a
# forced kill trip (grind mob, boss, skinning) to a creature no active quest needs, with a [QuestIdle] no_target note.
require_text("${choose}" "bool IsKillTripDestination(TravelDestination const* destination)\n{\n    return dynamic_cast<GrindTravelDestination const*>(destination) || dynamic_cast<BossTravelDestination const*>(destination) ||\n        (destination->GetPurpose() == TravelDestinationPurpose::GatherSkinning && destination->GetEntry() > 0);\n}" "kill trips: grind mob, boss, skinning (never a gather node, NPC or petition target)")
region("${choose}" "bool ChooseTravelTargetAction::SetBestTarget(" "\n    return hasTarget;\n}" best_target)
require_text("${best_target}" "bool const forcedNoGrind = target->IsForced() && ai->IsQuestNoGrind();" "forced targets only; GrindMode 0 / 1 false before the roster lookup")
require_text("${best_target}" "    if (forcedNoGrind)\n        forcedNeeded = AI_VALUE(std::vector<int32>, \"needed quest entries\");" "one per-bot list per choice, only when it can matter")
require_text("${best_target}" "if (forcedNoGrind && !ai::quest_grind_mode::ForcedKillTripAllowed(true, IsKillTripDestination(destination),\n                    std::binary_search(forcedNeeded.begin(), forcedNeeded.end(), destination->GetEntry())))\n            {\n                if (!forcedKillSkipped++)\n                    forcedKillEntry = destination->GetEntry();\n                continue;\n            }" "a forced kill trip only to a quest objective mob")
require_order("${best_target}" "if (forcedNoGrind && !ai::quest_grind_mode::ForcedKillTripAllowed(" "if (target->IsForced() || (travelInfoReuse ? reuseActive(destination)" "before the forced acceptance that skips IsActive")
require_text("${best_target}" "    if (!hasTarget && forcedKillSkipped)\n        ai->NoteQuestIdle(uint8(ai::quest_grind_mode::Reason::NoTarget),\n            ai::quest_grind_mode::TargetText(\"forced_kill_trip\", forcedKillEntry > 0 ? uint32(forcedKillEntry) : 0u));" "one no_target note per choice that found nothing else")
require_text("${choose}" "        SET_AI_VALUE2(bool, \"no active travel destinations\", futureTravelPurpose, true);" "a failed choice marks the purpose (the order does not re-request every tick)")
require_text("${policy}" "inline bool ForcedKillTripAllowed(bool noGrind, bool killTrip, bool neededForQuest)\n{\n    return !noGrind || !killTrip || neededForQuest;\n}" "forced kill trip rule")
count_text("${choose}" "ai->IsQuestNoGrind()" 3 "GrindMode tests in the choose file: the grind branch, the grind request, the forced kill trip")
region("${choose}" "bool RequestNamedTravelTargetAction::Execute(Event& event)" "\nbool RequestNamedTravelTargetAction::isAllowed() const" named_request)
forbid_text("${named_request}" "IsQuestNoGrind" "named travel (RosterGuild, guild meeting, city ...) gated")
require_text("${choose}" "else if (futureTravelPurpose == \"petition\" && sPlayerbotAIConfig.rosterGuildFoundTravel && RosterGuildPlan::UsesRosterPath(ai))\n    {\n        newTarget.SetForced(true);" "RosterGuild travel stays (forced, IsActive not asked)")
# A found quest target ends the idle episode (only with GrindMode 2).
require_text("${choose}" "        if (ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))\n            ai->ClearQuestIdle();" "idle episode reset behind GrindMode 2")
region("${choose}" "    if (futureTravelPurpose == \"quest\")\n    {\n        TraceQuestRouteDecision(bot, newTarget" "    else if (futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)) && UsesQuestFirstProgression(bot))" quest_found)
require_text("${quest_found}" "ai->ClearQuestIdle();" "cleared on a found quest route")

# ---- 6. Path 6: the Grind bit of the shared entry purpose map ---------------------------------------
region("${values}" "EntryTravelPurposeMap EntryTravelPurposeMapValue::Calculate()" "\nuint32 EntryTravelPurposeMapValue::SkillIdToGatherEntry" purpose_map)
require_text("${purpose_map}" "if (cInfo->MinLootGold > 0)\n        {\n            purpose |= (uint32)TravelDestinationPurpose::Grind;" "shared mask unchanged (money-dropping creatures; RPG uses NpcFlags)")
forbid_text("${purpose_map}" "IsQuestNoGrind" "the shared, all-bot purpose map gated per bot")
require_text("${travel_mgr}" "                case TravelDestinationPurpose::Grind:\n                    dests.push_back(AddDestination<GrindTravelDestination>(entry, purposeFlag));" "the Grind bit becomes a GrindTravelDestination")
region("${travel_mgr}" "bool GrindTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const" "\n}\n" grind_active)
require_order("${grind_active}" "if (!IsPossible(info))\n        return false;" "if (ai->IsQuestNoGrind())" "after IsPossible")
require_order("${grind_active}" "if (ai->IsQuestNoGrind())" "return GuidPosition(bot).IsHostileTo(GuidPosition(HIGHGUID_UNIT, GetEntry()), bot->GetInstanceId());" "before the hostility test")
require_text("${grind_active}" "std::vector<int32> const& needed = AI_VALUE(std::vector<int32>, \"needed quest entries\");" "one sorted per-bot list")
require_text("${grind_active}" "if (!ai::quest_grind_mode::GrindDestinationActive(true, std::binary_search(needed.begin(), needed.end(), GetEntry())))\n            return false;" "only quest objective mob destinations stay")
forbid_text("${grind_active}" "\"need for quest\"" "a value per creature")
count_text("${travel_mgr}" "ai->IsQuestNoGrind()" 1 "only the grind destination is gated in TravelMgr")
require_text("${policy}" "inline bool GrindDestinationActive(bool noGrind, bool neededForQuest)\n{\n    return !noGrind || neededForQuest;\n}" "destination rule")

# ---- 7. Path 3: the opportunistic "attack anything" (grind target) ---------------------------------
require_text("${grind_target}" "bool const noGrind = ai->IsQuestNoGrind();" "one GrindMode test per value pass")
count_text("${grind_target}" "ai->IsQuestNoGrind()" 1 "one GrindMode test per value pass")
region("${grind_target}" "std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>(\"possible attack targets\")->Get();" "std::list<ObjectGuid> targets = *context->GetValue<std::list<ObjectGuid> >(\"possible targets\");" attacker_loop)
require_text("${attacker_loop}" "logGrind(unit, \"(hostile) selected.\");\n        return unit;" "attackers stay (self-defence, group help)")
forbid_text("${attacker_loop}" "noGrind" "the attacker loop gated")
require_order("${grind_target}" "needForQuestCache[entry] = needForQuest;" "if (!ai::quest_grind_mode::TargetAllowed(noGrind, false, needForQuest))" "the filter uses the loop's own quest test")
require_order("${grind_target}" "if (!ai::quest_grind_mode::TargetAllowed(noGrind, false, needForQuest))" "if (urand(0, 100) < 20 && travelTargetWorking && !isGrindTravelDest)" "before any urand of the candidate")
require_text("${grind_target}" "            logGrind(unit, \"ignored (GrindMode 2: not needed for a quest).\");\n            continue;" "a non-quest candidate is skipped")
# Review fix (F3): not while the bot is only on its way (the value runs before the action's "traveling -> no attack" test).
require_text("${grind_target}" "if (!result && noGrindSkipped && !assistCount &&\n        ai::quest_grind_mode::TargetNoteDue(travelTargetWorking, travelTargetTraveling))\n        ai->NoteQuestIdle(uint8(ai::quest_grind_mode::TargetReason(travelTargetWorking))," "reason opportunistic / no_target once per pass, silent on the way")
require_text("${policy}" "inline bool TargetNoteDue(bool travelTargetWorking, bool travelTargetTraveling)\n{\n    return travelTargetWorking || !travelTargetTraveling;\n}" "traveling alone is not counted")
require_text("${policy}" "inline bool TargetAllowed(bool noGrind, bool attacker, bool neededForQuest)\n{\n    return !noGrind || attacker || neededForQuest;\n}" "target rule")
require_text("${policy}" "return travelTargetActive ? Reason::Opportunistic : Reason::NoTarget;" "opportunistic on the way, no_target idle")

# ---- 8. Path 4: the "grind" strategy itself (strategy level, not the action) ------------------------
require_text("${grind_strategy}" "    triggers.push_back(new TriggerNode(\n        \"no target\",\n        NextAction::array(0,\n        new NextAction(\"attack anything\", 5.0f), NULL)));" "grind -> attack anything 5.0 unchanged (quest objective kills need it)")
require_text("${grind_strategy_h}" "std::string getName() override { return \"grind\"; }" "strategy name")
require_text("${grind_strategy_h}" "void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;" "the strategy owns its multiplier")
require_text("${grind_strategy_h}" "class QuestGrindModeMultiplier : public Multiplier" "multiplier class")
region("${grind_strategy}" "void GrindingStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)" "\n}\n" grind_mult_init)
require_text("${grind_mult_init}" "if (ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))\n        multipliers.push_back(new QuestGrindModeMultiplier(ai));" "added only with GrindMode 2 (0 / 1: same engine)")
region("${grind_strategy}" "float QuestGrindModeMultiplier::GetValue(Action* action)" "\n}\n" grind_mult)
require_text("${grind_mult}" "if (!action || action->getName() != \"attack anything\" || !ai->IsQuestNoGrind())\n        return 1.0f;" "only the grind strategy's action, only the population")
require_text("${grind_mult}" "AI_VALUE(std::list<ObjectGuid>, \"possible attack targets\");" "attackers stay")
require_text("${grind_mult}" "AI_VALUE(std::vector<int32>, \"needed quest entries\");" "quest objectives stay (one per-bot list)")
require_text("${grind_mult}" "if (ai::quest_grind_mode::TargetAllowed(true, attacker, questObjective))\n        return 1.0f;" "allowed targets pass")
require_text("${grind_mult}" "ai::quest_grind_mode::TargetText(\"attack_anything\", target->GetEntry()));\n    return 0.0f;" "everything else is multiplied to 0 with a reason")
forbid_text("${grind_mult}" "\"need for quest\"" "a value per creature")
forbid_text("${grind_strategy}" "std::mutex" "a lock in the strategy")
# The action itself has no GrindMode gate: other strategies keep "attack anything" for real combat.
forbid_text("${choose_target}" "IsQuestNoGrind" "a GrindMode gate in AttackAnythingAction (the strategy decides)")
require_text("${rogue}" "(actionName == \"attack anything\") ||" "rogue stealth allow list keeps attack anything")
require_text("${dps_assist}" "NextAction::array(0, new NextAction(\"dps assist\", 60.0f), NULL)));" "group help: dps assist unchanged")
require_text("${tank_assist}" "NextAction::array(0, new NextAction(\"tank assist\", 60.0f), NULL)));" "group help: tank assist unchanged")

# ---- 9. Path 5: world buff travel keeps "attack anything" at 0 --------------------------------------
region("${wbuff}" "float WorldBuffTravelMultiplier::GetValue(Action* action)" "\n}\n" wbuff_mult)
require_order("${wbuff_mult}" "name == \"grind\" || name == \"attack anything\")" "        return 0.0f;" "attack anything multiplied to 0 during world buff travel (every GrindMode)")

# ---- 10. [QuestIdle]: once per reason and idle episode per bot, minute counter on the world thread ---------------------
require_text("${policy}" "return \"[QuestIdle] reason=\" + std::string(ReasonName(reason)) + \" bot=\" + std::to_string(bot) + \" level=\" +\n        std::to_string(level) + \" zone=\" + std::to_string(zone) + \" target=\" + (target.empty() ? std::string(\"-\") : target);" "idle line format")
require_text("${policy}" "return \"[QuestIdle] minute fallback=\" + std::to_string(counts[std::size_t(Reason::Fallback)]) +" "minute line format")
foreach(name "fallback" "money" "opportunistic" "no_target")
  require_text("${policy}" "return \"${name}\";" "reason name ${name}")
endforeach()
# Review fix (F1): one line per reason and idle episode (bit mask), not per reason change - alternating reasons
# (money / no_target / money ...) logged about once a second per idle bot under the old last-reason rule.
require_text("${policy}" "return reason != Reason::None && !(loggedMask & ReasonBit(reason));" "a line only for a reason not yet logged in this idle episode")
require_text("${policy}" "return reason == Reason::None ? std::uint8_t(0) : std::uint8_t(1u << std::uint8_t(reason));" "one bit per reason, None none")
require_text("${policy}" "return std::uint8_t(loggedMask | ReasonBit(reason));" "the logged reason is marked")
forbid_text("${policy}" "!= lastReason" "the old last-reason rule (unbounded with alternating reasons)")
region("${ai_cpp}" "void PlayerbotAI::NoteQuestIdle(uint8 reason, std::string const& target)" "\n}\n" note_fn)
require_order("${note_fn}" "if (!ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode) || !bot ||\n        reason >= ai::quest_grind_mode::ReasonCount)\n        return;" "questIdleCounts[reason].fetch_add(1, std::memory_order_relaxed);" "only with GrindMode 2, bounded index")
require_order("${note_fn}" "if (!ai::quest_grind_mode::LogDue(questIdleLogged, why))\n        return;\n\n    questIdleLogged = ai::quest_grind_mode::MarkLogged(questIdleLogged, why);" "sLog.outBasic(\"%s\", ai::quest_grind_mode::FormatIdleLine(" "the line only once per reason and idle episode")
count_text("${ai_cpp}" "questIdleLogged" 3 "the mask is read and marked only in NoteQuestIdle (cleared in the header)")
require_text("${ai_cpp}" "std::atomic<uint32> questIdleCounts[ai::quest_grind_mode::ReasonCount] = {};" "map-thread counters are atomics")
require_text("${ai_cpp}" "counts[reason] = questIdleCounts[reason].exchange(0, std::memory_order_relaxed);" "minute counters taken and reset")
require_text("${ai_h}" "uint8 questIdleLogged = 0;" "per-bot mask of logged reasons (one byte, bot's own thread)")
require_text("${ai_h}" "void ClearQuestIdle() { questIdleLogged = 0; }" "idle episode reset")
forbid_text("${ai_h}" "questIdleReason" "the old last-reason byte")
region("${mgr}" "void ReportQuestIdle()" "\n}\n" report_fn)
# Review nit: GrindMode first, so with 0 / 1 not even the clock is read.
require_order("${report_fn}" "    if (!ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))\n        return;" "uint32 const now = uint32(time(nullptr));" "only with GrindMode 2, before the clock read")
require_text("${report_fn}" "    if (now < lastReport + 60)\n        return;\n    lastReport = now;" "once a minute")
require_text("${report_fn}" "sLog.outBasic(\"%s\", PlayerbotAI::TakeQuestIdleMinuteLine().c_str());" "minute line")
region("${mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "sPerformanceMonitor.Init(0, 0);" upd)
require_order("${upd}" "\n    ReportDirectTargets();" "\n    ReportQuestIdle();" "minute line on the world thread, next to the other minute lines")
foreach(src values grind_target choose grind_strategy travel_mgr mgr)
  forbid_text("${${src}}" "sLog.outBasic(\"[QuestIdle]" "a [QuestIdle] line outside the bounded helper (${src})")
endforeach()
forbid_text("${ai_cpp}" "sLog.outBasic(\"[QuestIdle]" "a [QuestIdle] line outside the policy format")
# Every reason has its site.
require_text("${values}" "ai::quest_grind_mode::Reason::Fallback" "fallback site")
require_text("${request_useful}" "RequestReason(" "money site")
require_text("${grind_target}" "TargetReason(" "opportunistic / no_target site (grind target)")
require_text("${grind_mult}" "TargetReason(" "opportunistic / no_target site (strategy)")

# ---- 11. Neutral default, restart-only, read --------------------------------------------------------
require_text("${config_h}" "uint32 questFirstProgressionGrindMode = 0;" "member default 0")
# Not clamped on purpose (review nit rejected): only the plain `member = config.GetIntDefault("Key"` form is seen by the
# CI read scan (t/config_key_usage_tests.py ASSIGN); a negative value wraps and acts like 0 (unit test).
require_text("${config_cpp}" "\n    questFirstProgressionGrindMode = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.GrindMode\", 0);" "read with default 0, in the form the CI read scan sees")
require_text("${conf}" "\nAiPlayerbot.QuestFirstProgression.GrindMode = 0\n" "documented with 0")
forbid_text("${conf}" "AiPlayerbot.QuestFirstProgression.GrindMode = 1" "shipped as 1")
forbid_text("${conf}" "AiPlayerbot.QuestFirstProgression.GrindMode = 2" "shipped as 2")
require_text("${conf}" "#   1 = RESERVED for the gap filler of stages S2/S4; until then exactly like 0." "mode 1 documented as reserved")
require_order("${conf}" "AiPlayerbot.QuestFirstProgression.AbandonStructuralParked = 0" "AiPlayerbot.QuestFirstProgression.GrindMode = 0" "documented next to the S1 keys")

# ---- 12. S1 recheck nit: the abandon mark on every autonomous accept path ---------------------------
region("${accept}" "static bool SkipAbandonedQuest(PlayerbotAI* ai, Quest const* quest)" "\n}\n" abandon_fn)
require_text("${abandon_fn}" "return sPlayerbotAIConfig.questFirstProgressionAbandonStructuralParked && !ai->HasRealPlayerMaster() &&\n        ai->GetAiObjectContext()->GetValue<TravelTarget*>(\"travel target\")->Get()->IsStructurallyAbandoned(quest->GetQuestId());" "behind the switch, bots on their own, the travel target's mark")
region("${accept}" "static bool SkipQuestForRosterBot(PlayerbotAI* ai, Player* bot, Quest const* quest)" "\n}\n" skip_fn)
require_order("${skip_fn}" "if (SkipAbandonedQuest(ai, quest))\n        return true;" "bool const rosterOnItsOwn" "the mark first in the one accept rule (dialog, packet, accept all)")
region("${accept}" "bool AcceptQuestShareAction::Execute(Event& event)" "\nbool ConfirmQuestAction::Execute" share_fn)
require_order("${share_fn}" "quest = qInfo->GetQuestId();" "if (SkipAbandonedQuest(ai, qInfo))\n    {\n        bot->SetDividerGuid(ObjectGuid());\n        return false;\n    }" "share path checks the mark after the catch-up branch")
require_order("${share_fn}" "if (SkipAbandonedQuest(ai, qInfo))" "bot->AddQuest( qInfo, requester);" "before the share adds the quest")
forbid_text("${share_fn}" "SkipQuestForRosterBot(" "the whole accept rule on the share path (would change shares with the switch at 0)")
# Review fix (F4): the escort confirm (SMSG_QUEST_CONFIRM_ACCEPT from a group member) checks the mark too.
region("${accept}" "bool ConfirmQuestAction::Execute(Event& event)" "\nbool QuestDetailsAction::Execute" confirm_fn)
require_order("${confirm_fn}" "if( !bot->CanTakeQuest( qInfo, false ) )" "    if (SkipAbandonedQuest(ai, qInfo))\n        return false;\n\n    if( bot->CanAddQuest( qInfo, false ) )" "escort confirm: the mark after CanTakeQuest, before AddQuest")
count_text("${accept}" "SkipAbandonedQuest(" 4 "definition, accept rule, share path, escort confirm")
# Decided (review F4): the guild quest order (GuildAcceptQuestOrderAction) comes from an officer note, which only a
# player writes - an explicit order, not an autonomous pick. It keeps taking the quest; the mark would also leave the
# forced guild trip to the giver without an accept. UseAction's quest item is a master's "use" command.
forbid_text("${guild_quest_order}" "IsStructurallyAbandoned" "the abandon mark on the guild quest order (decided: the officer's order wins)")
require_text("${guild_quest_order}" "bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(p);" "the guild quest order accepts through the opcode handler")
region("${accept}" "bool QuestDetailsAction::Execute(Event& event)" "\n}\n" details_fn)
require_order("${details_fn}" "if (SkipQuestForRosterBot(ai, bot, qInfo))" "bot->AddQuest(qInfo, requester);" "the dialog path asks the accept rule before AddQuest")
require_text("${accept}" "#include \"playerbot/TravelMgr.h\"" "include what you use (TravelTarget)")

message(STATUS "QUEST_GRIND_MODE_SOURCE_CONTRACT=PASS")
