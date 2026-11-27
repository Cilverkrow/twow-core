# twow-repo#780 (bug fix, owner finding 26.11.2026 on realm C; OB-10 triage a2ada213, CLI-GRP plan with the third gate, OB-00 OK
# 26.11.2026; code default 0 until the owner group test, then 1, 0 = rollback): AiPlayerbot.Loot.GroupQuestLoot. A bot in a group
# with an active player master did not loot its own quest items: a 30 yd hostile gate, a 10 yd cap measured from the player and
# the follow leash (FollowDistance) blocked every corpse out of the player's immediate reach.
# This contract pins the key (dist 0, member 0, read with 0, [ConfigEcho] sorted), the pure policy, the single eligibility helper
# with the key tested first (so the key 0 takes every old path), the old range and hostile rules kept for every other corpse,
# the FarFromCurrentLootTrigger bound, the leash hook in "out of free move range" (key first, after the old check), the same-Map
# guard of the leash helper, the two line formats and the unit test.

cmake_policy(VERSION 3.16)

set(TAG "GroupQuestLoot")

if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "${TAG}: PB_SOURCE_DIR and PB_MODULE_DIR are required")
endif()

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "${TAG}: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(rest "${text}")
  set(n 0)
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${len}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "${TAG}: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

# region(text start end out): from start up to and including the first end after it.
function(region text start end out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "${TAG}: region start not found: ${start}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "${TAG}: region end not found after: ${start}")
  endif()
  string(LENGTH "${end}" elen)
  math(EXPR blen "${b} + ${elen}")
  string(SUBSTRING "${tail}" 0 ${blen} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/GroupQuestLootPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/strategy/actions/AddLootAction.cpp" add_loot)
read_source("${PB_SOURCE_DIR}/strategy/actions/LootAction.cpp" loot_action)
read_source("${PB_SOURCE_DIR}/LootObjectStack.h" stack_h)
read_source("${PB_SOURCE_DIR}/strategy/triggers/LootTriggers.h" loot_trig_h)
read_source("${PB_SOURCE_DIR}/strategy/triggers/LootTriggers.cpp" loot_trig_cpp)
read_source("${PB_SOURCE_DIR}/strategy/triggers/RangeTriggers.h" range_h)
read_source("${PB_SOURCE_DIR}/strategy/generic/FollowMasterStrategy.cpp" follow_strategy)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)

# 1. The key: dist 0, member 0, read with 0 (bug fix; 1 after the owner group test, 0 = rollback), [ConfigEcho] sorted.
require_text("${dist}" "\nAiPlayerbot.Loot.GroupQuestLoot = 0\n" "dist default 0")
count_text("${dist}" "AiPlayerbot.Loot.GroupQuestLoot =" 1 "one dist line")
require_text("${config_h}" "    bool lootGroupQuestLoot = false;" "member default 0")
require_text("${config_cpp}" "    lootGroupQuestLoot = config.GetBoolDefault(\"AiPlayerbot.Loot.GroupQuestLoot\", false);" "read with default 0")
require_text("${config_cpp}" "            {\"LogInGroupOnly\", Value(logInGroupOnly)},\n            {\"Loot.GroupQuestLoot\", Value(lootGroupQuestLoot)},\n            {\"Loot.GroupQuestLoot.BotLeader\", Value(lootGroupQuestLootBotLeader)},\n            {\"Loot.HoldTravelWhileLootable\", Value(lootHoldTravelWhileLootable)}," "[ConfigEcho] entries in sorted place (twow-repo#627 GRP2030 BotLeader after it)")
count_text("${config_cpp}" "\"AiPlayerbot.Loot.GroupQuestLoot\"" 1 "one config read")
# twow-repo#627 GRP2030 (lever, default 0): Loot.GroupQuestLoot.BotLeader - dist 0, member 0, read with 0.
require_text("${dist}" "\nAiPlayerbot.Loot.GroupQuestLoot.BotLeader = 0\n" "BotLeader dist default 0")
count_text("${dist}" "AiPlayerbot.Loot.GroupQuestLoot.BotLeader =" 1 "one BotLeader dist line")
require_text("${config_h}" "    bool lootGroupQuestLootBotLeader = false;" "BotLeader member default 0")
require_text("${config_cpp}" "    lootGroupQuestLootBotLeader = config.GetBoolDefault(\"AiPlayerbot.Loot.GroupQuestLoot.BotLeader\", false);" "BotLeader read with default 0")
count_text("${config_cpp}" "\"AiPlayerbot.Loot.GroupQuestLoot.BotLeader\"" 1 "one BotLeader config read")

# 2. The policy is pure (only <cstddef> and <cstdint>) and keeps the plan's constants.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" policy_includes "${policy}")
if(NOT policy_includes STREQUAL "#include <cstddef>;#include <cstdint>")
  message(FATAL_ERROR "${TAG}: GroupQuestLootPolicy.h is not pure: ${policy_includes}")
endif()
require_text("${policy}" "constexpr float AggroMarginYards = 2.0f;" "aggro margin 2 yd")
require_text("${policy}" "constexpr std::int64_t LeashHoldMaxSeconds = 30;" "leash hold at most 30 s")
require_text("${policy}" "constexpr std::int64_t HoldForgetSeconds = 300;" "a hold / expiry is forgotten after the corpse life (respawn keeps the guid)")
require_text("${policy}" "constexpr std::int64_t QuietGapMaxSeconds = 5;" "a step adds at most 5 s of quiet time")
require_text("${policy}" "    h.quiet += gap < 0 ? 0 : (gap > QuietGapMaxSeconds ? QuietGapMaxSeconds : gap);\n    h.last = now;\n    if (h.quiet < LeashHoldMaxSeconds)\n        return Leash::Hold;" "only quiet time counts (a fight does not use the hold up)")
require_text("${policy}" "    return inCombat || hostileToCorpse <= aggroRange + AggroMarginYards;" "hostile rule (a)")
require_text("${policy}" "    return botToCorpse <= lootDistance && masterToCorpse <= lootDistance;" "range rule (b) with the player bound")
require_text("${policy}" "    return questCorpse ? lootDistance : followDistance + maxLootDistance;" "trigger bound (c), the old sum otherwise")
require_text("${policy}" "    return partyLoot && !lootedByAll && allowedForBot && !lootedByBot;" "own party-loot quest drop only (no race with the player)")
require_text("${policy}" "    if (!eligible || guid == 0 || !QuestCorpseInRange(botToCorpse, masterToCorpse, lootDistance))\n        return Leash::Off;\n" "Off keeps the state (the cap is per corpse)")

# 3. One eligibility helper, key first; the only key reads are the helper and the leash hook.
region("${loot_trig_cpp}" "static bool GroupQuestLootEligible(PlayerbotAI* ai, Player* bot, WorldObject* wo)" "\n}\n" elig_fn)
require_text("${elig_fn}" "    return GroupQuestLootAnyKey() && wo && wo->IsCreature() && bot->GetGroup() && !ai->IsGroupLeader()\n        && GroupQuestLootMasterOk(ai, bot) && HasOwnPartyQuestLoot(bot, (Creature*)wo);" "eligibility: a key first, non-leader, master side, own quest item")
# twow-repo#627 GRP2030: the master side - a player master only with Loot.GroupQuestLoot (#780 unchanged), a bot master only with
# Loot.GroupQuestLoot.BotLeader and the manager's QUEST follower role (1); nothing else of another bot is read.
region("${loot_trig_cpp}" "static bool GroupQuestLootMasterOk(PlayerbotAI* ai, Player* bot)" "\n}\n" master_fn)
require_text("${master_fn}" "    if (ai->HasActivePlayerMaster())\n        return sPlayerbotAIConfig.lootGroupQuestLoot;\n    return sPlayerbotAIConfig.lootGroupQuestLootBotLeader && group_mgr::QuestRoleOf(bot->GetGUIDLow()) == 1;" "master side: player master #780, bot leader only for a manager QUEST follower")
region("${loot_trig_cpp}" "bool ai::GroupQuestLootAnyKey()" "\n}\n" anykey_fn)
require_text("${anykey_fn}" "    return sPlayerbotAIConfig.lootGroupQuestLoot || sPlayerbotAIConfig.lootGroupQuestLootBotLeader;" "either key")
region("${loot_trig_cpp}" "bool ai::GroupQuestLootCorpse(PlayerbotAI* ai, Player* bot, WorldObject* wo)" "\n}\n" corpse_fn)
require_text("${corpse_fn}" "    return GroupQuestLootEligible(ai, bot, wo)\n        && !group_quest_loot::HoldExpired(GroupQuestHold(ai), wo->GetObjectGuid().GetRawValue(), int64(time(nullptr)));" "an expired corpse takes the old rules")
require_text("${loot_trig_cpp}" "    return ai->GetAiObjectContext()->GetValue<LootObjectStack*>(\"available loot\")->Get()->GroupQuestHold();" "the hold lives in the bot's LootObjectStack")
require_text("${stack_h}" "        group_quest_loot::Hold& GroupQuestHold() { return groupQuestHold; }" "hold accessor")
require_text("${stack_h}" "        group_quest_loot::Hold groupQuestHold;" "hold member")
# The quest drops are read from Loot::m_questItems (the shim's GetLootItemsListFor returns only `items`), party loot only.
region("${loot_trig_cpp}" "static bool HasOwnPartyQuestLoot(Player* bot, Creature* creature)" "\n}\n" own_fn)
require_text("${own_fn}" "    Loot const* loot = creature->m_loot;" "the corpse's own Loot")
require_text("${own_fn}" "        LootItem const& item = loot->m_questItems[i];" "the quest drops")
require_text("${own_fn}" "    QuestItemMap const& perPlayer = loot->GetPlayerQuestItems();" "the bot's own looted state")
require_text("${own_fn}" "group_quest_loot::OwnPartyQuestItem(item.freeforall, item.is_looted, item.AllowedForPlayer(bot, creature), lootedByBot)" "policy inputs")
forbid_text("${loot_trig_cpp}" "HasQuestItemsInWOLootList" "the shim list has no quest drops")
# Key reads in LootTriggers.cpp: GroupQuestLootAnyKey (both keys) and GroupQuestLootMasterOk (both keys) only.
count_text("${loot_trig_cpp}" "sPlayerbotAIConfig.lootGroupQuestLoot" 4 "the key reads in LootTriggers.cpp (AnyKey + MasterOk)")
count_text("${loot_trig_cpp}" "sPlayerbotAIConfig.lootGroupQuestLootBotLeader" 2 "the BotLeader reads (AnyKey + MasterOk)")
count_text("${add_loot}" "sPlayerbotAIConfig.lootGroupQuestLoot" 0 "AddLoot reads the key only through the helper")
count_text("${range_h}" "sPlayerbotAIConfig.lootGroupQuestLoot" 0 "the leash hook reads the keys only through GroupQuestLootAnyKey")
require_text("${loot_trig_h}" "    bool GroupQuestLootAnyKey();" "any-key helper declared")
require_text("${loot_trig_h}" "    bool GroupQuestLootCorpse(PlayerbotAI* ai, Player* bot, WorldObject* wo);" "helper declared")
require_text("${loot_trig_h}" "    bool GroupQuestLootLeashQuiet(PlayerbotAI* ai, Player* bot);" "leash helper declared")

# 4. AddLoot: quest corpse -> (b) from the bot; every other corpse the old cap from the player, unchanged. The hostile search, its
#    30 yd and the whisper stay; the filter runs only for a quest corpse.
region("${add_loot}" "bool AddAllLootAction::AddLoot(Player* requester, ObjectGuid guid)" "\nbool AddGatheringLootAction::Execute(Event& event)" add_fn)
require_text("${add_fn}" "    bool groupQuestCorpse = GroupQuestLootCorpse(ai, bot, wo);" "AddLoot uses the helper")
require_text("${add_fn}" "        if (!group_quest_loot::QuestCorpseInRange(botDist, masterDist, sPlayerbotAIConfig.lootDistance))" "quest range from the bot")
require_text("${add_fn}" "    else if (sServerFacade.IsDistanceGreaterThan(masterDist, lootDistanceToUse))\n    {\n        ai->TellDebug(requester, \"Outside of loot range: \" + std::to_string(lootDistanceToUse), \"debug loot\");" "old cap kept for every other corpse")
require_text("${add_fn}" "        float MOB_AGGRO_DISTANCE = 30.0f;\n        std::list<Unit*> hostiles = ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, MOB_AGGRO_DISTANCE);" "hostile search unchanged")
require_text("${add_fn}" "        if (groupQuestCorpse && !hostiles.empty())" "hostile filter only for a quest corpse")
require_text("${add_fn}" "group_quest_loot::HostileBlocks(hostile->IsInCombat(), hostile->GetDistance(wo, SizeFactor::None), hostile->GetAttackDistance(bot))" "hostile rule (a) inputs")
require_order("${add_fn}" "        if (groupQuestCorpse && !hostiles.empty())" "        if (hostiles.size() > 0)" "filter before the old reject")
require_text("${add_fn}" "out << hostiles.front()->GetName() << \" is blocking \" << wo->GetName() << \", need to kill it or I will not loot\";" "old whisper kept")
require_text("${add_fn}" "    if (added && (groupQuestRangeByKey || groupQuestHostilesIgnored > 0) && groupQuestLootSeen.Insert(guid.GetRawValue()))\n        sLog.outBasic(\"[GroupQuestLoot] bot=%u corpse=%u entry=%u gate=add range_by_key=%u hostiles_ignored=%u bot_dist=%.1f master_dist=%.1f\"," "effect line only when the key decided")

# 5. FarFromCurrentLootTrigger (c): the bound from the policy; key 0 = followDistance + loot distance.
region("${loot_trig_cpp}" "bool FarFromCurrentLootTrigger::IsActive()" "\n}\n" far_fn)
require_text("${far_fn}" "            float safeRange = group_quest_loot::MasterBound(GroupQuestLootCorpse(ai, bot, creature), sPlayerbotAIConfig.followDistance,\n                bot->GetMaxLootDistance(creature), sPlayerbotAIConfig.lootDistance);" "trigger bound (c)")
# (c') LootAction picks the loot target under the same bound (it had the same followDistance + loot distance filter).
region("${loot_action}" "bool LootAction::Execute(Event& event)" "\n}\n" pick_fn)
require_text("${pick_fn}" "                float safeRange = group_quest_loot::MasterBound(GroupQuestLootCorpse(ai, bot, c), sPlayerbotAIConfig.followDistance,\n                    bot->GetMaxLootDistance(c), sPlayerbotAIConfig.lootDistance);\n                if (sServerFacade.GetDistance2d(master, c) > safeRange)\n                    continue;" "pick bound (c')")
count_text("${loot_action}" "sPlayerbotAIConfig.lootGroupQuestLoot" 0 "LootAction reads the key only through the helper")

# 6. The leash (d): the old check first, then the key, then the helper; the helper guards the master by Map object, not in combat.
region("${range_h}" "    class OutOfFreeMoveRangeTrigger : public Trigger" "\n    };\n" leash_cls)
require_text("${leash_cls}" "            if (AI_VALUE(bool, \"can free move\"))\n                return false;\n" "old check first")
require_text("${leash_cls}" "            if (GroupQuestLootAnyKey() && GroupQuestLootLeashQuiet(ai, bot))\n                return false;\n            return true;" "a key before the helper, else as before")
forbid_text("${leash_cls}" "group_quest_loot::Hold" "no hold state in the trigger (the hold lives in LootObjectStack)")
region("${loot_trig_cpp}" "bool ai::GroupQuestLootLeashQuiet(PlayerbotAI* ai, Player* bot)" "\n}\n" leash_fn)
require_text("${leash_fn}" "bot->IsInWorld() && master->IsInWorld() && master->GetMap() == bot->GetMap() && !bot->IsInCombat()" "both in world (GetMap asserts mid-teleport), same-Map guard, not in combat")
require_text("${leash_fn}" "sServerFacade.GetDeathState(creature) == CORPSE && GroupQuestLootEligible(ai, bot, creature)" "a quest corpse only (the hold decides expiry)")
require_text("${leash_fn}" "group_quest_loot::LeashStep(GroupQuestHold(ai), eligible, corpseGuid, botDist, masterDist,\n        sPlayerbotAIConfig.lootDistance, int64(time(nullptr)));" "the policy decides with LootDistance")
require_text("${leash_fn}" "    if (step == group_quest_loot::Leash::Expire)\n        context->GetValue<LootObject>(\"loot target\")->Set(LootObject());" "expiry resets the loot target (no follow / loot oscillation)")
forbid_text("${leash_fn}" "GiveUp(" "no GiveUp (MoveToLootAction only, #703 counters)")
require_text("${leash_fn}" "\"[GroupQuestLoot] bot=%u corpse_guid=%llu entry=%u gate=leash result=%s bot_dist=%.1f master_dist=%.1f\"" "leash line format")
forbid_text("${leash_fn}" "GetMapId()" "no map-id comparison (harm_race_ratchet)")
# The follow strategy itself is unchanged: the trigger stays at ACTION_HIGH with follow.
require_text("${follow_strategy}" "        \"out of free move range\",\n        NextAction::array(0, new NextAction(\"check mount state\", ACTION_HIGH), new NextAction(\"follow\", ACTION_HIGH), NULL)));" "follow strategy unchanged")

# 7. The unit test is registered.
require_text("${tests}" "add_executable(group_quest_loot_policy_tests\n  \"\${PB_MODULE_DIR}/t/group_quest_loot_policy_tests.cpp\")" "unit test target")
require_text("${tests}" "add_test(NAME group_quest_loot_policy\n  COMMAND group_quest_loot_policy_tests" "unit test registered")

message(STATUS "GROUP_QUEST_LOOT_SOURCE_CONTRACT=PASS")
