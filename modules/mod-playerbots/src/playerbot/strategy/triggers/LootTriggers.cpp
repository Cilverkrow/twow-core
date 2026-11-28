
#include "playerbot/playerbot.h"
#include "LootTriggers.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/GroupManagerView.h"   // twow-repo#627 GRP2030: QuestRoleOf (Loot.GroupQuestLoot.BotLeader)
#include "playerbot/BotSlots.h"   // SameMapObject (BotLeader: the leader bot on the bot's own Map object)
#include "playerbot/strategy/values/LootValues.h"

#include "playerbot/ServerFacade.h"
using namespace ai;

// twow-repo#780 (AiPlayerbot.Loot.GroupQuestLoot, GroupQuestLootPolicy.h): a party-loot quest drop of the corpse that is the bot's
// own copy to take. The corpse is on the bot's Map (it came from the bot's loot list / loot target); only its Loot is read.
static bool HasOwnPartyQuestLoot(Player* bot, Creature* creature)
{
    Loot const* loot = creature->m_loot;
    if (!loot || loot->m_questItems.empty())
        return false;
    QuestItemMap const& perPlayer = loot->GetPlayerQuestItems();
    QuestItemMap::const_iterator own = perPlayer.find(bot->GetGUIDLow());
    for (size_t i = 0; i < loot->m_questItems.size(); ++i)
    {
        LootItem const& item = loot->m_questItems[i];
        bool lootedByBot = false;
        if (own != perPlayer.end() && own->second)
        {
            for (QuestItem const& q : *own->second)
            {
                if (q.index == i && q.is_looted)
                    lootedByBot = true;
            }
        }
        if (group_quest_loot::OwnPartyQuestItem(item.freeforall, item.is_looted, item.AllowedForPlayer(bot, creature), lootedByBot))
            return true;
    }
    return false;
}

bool ai::GroupQuestLootAnyKey()
{
    return sPlayerbotAIConfig.lootGroupQuestLoot || sPlayerbotAIConfig.lootGroupQuestLootBotLeader;
}

// The master side of the eligibility: Loot.GroupQuestLoot with an active player master (#780), or twow-repo#627 GRP2030
// Loot.GroupQuestLoot.BotLeader (default 0) for a follower of a manager QUEST group, whose master is the bot leader. The role
// comes from the manager's view (GroupManagerView.h, 1 = follower; only bots of the manager's cohort can hold it): an ad-hoc or
// S7 follower without a manager group is not covered. The bot leader must stand on the bot's own Map object (OB-60 pre-review
// 27.11. P2): AddLoot (c) / LootAction (c') measure masterDist to it, and a leader on another Map with matching coordinates would
// count as near. Only its Map pointer is compared, no other field of the leader is read.
static bool GroupQuestLootMasterOk(PlayerbotAI* ai, Player* bot)
{
    if (ai->HasActivePlayerMaster())
        return sPlayerbotAIConfig.lootGroupQuestLoot;
    if (!sPlayerbotAIConfig.lootGroupQuestLootBotLeader || group_mgr::QuestRoleOf(bot->GetGUIDLow()) != 1)
        return false;
    Player* leader = ai->GetMaster();
    return leader && leader != bot && bot->IsInWorld() && leader->IsInWorld() && SameMapObject(bot, leader);
}

// Eligible without the hold: a key, a non-leader group member with an active player master (or a manager QUEST follower), the
// bot's own party quest drop.
static bool GroupQuestLootEligible(PlayerbotAI* ai, Player* bot, WorldObject* wo)
{
    return GroupQuestLootAnyKey() && wo && wo->IsCreature() && bot->GetGroup() && !ai->IsGroupLeader()
        && GroupQuestLootMasterOk(ai, bot) && HasOwnPartyQuestLoot(bot, (Creature*)wo);
}

static group_quest_loot::Hold& GroupQuestHold(PlayerbotAI* ai)
{
    return ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->GroupQuestHold();
}

// A quest corpse for (a)-(c'): eligible, and its leash hold of this bot has not expired (then the old rules apply).
bool ai::GroupQuestLootCorpse(PlayerbotAI* ai, Player* bot, WorldObject* wo)
{
    return GroupQuestLootEligible(ai, bot, wo)
        && !group_quest_loot::HoldExpired(GroupQuestHold(ai), wo->GetObjectGuid().GetRawValue(), int64(time(nullptr)));
}

// twow-repo#780 (d): the master must be on the bot's Map object (same-map guard), the loot target a lootable quest corpse.
bool ai::GroupQuestLootLeashQuiet(PlayerbotAI* ai, Player* bot)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    bool eligible = false;
    uint64 corpseGuid = 0;
    uint32 corpseEntry = 0;
    float botDist = 0.0f;
    float masterDist = 0.0f;
    Player* master = ai->GetMaster();
    // FindMap (SameMapObject), not GetMap: the master may be a bot on another region Map object, mid-transfer on its own thread.
    if (master && master != bot && bot->IsInWorld() && master->IsInWorld() && SameMapObject(bot, master) && !bot->IsInCombat())
    {
        LootObject loot = AI_VALUE(LootObject, "loot target");
        if (!loot.IsEmpty() && loot.IsLootPossible(bot))
        {
            Creature* creature = ai->GetCreature(loot.guid);
            if (creature && sServerFacade.GetDeathState(creature) == CORPSE && GroupQuestLootEligible(ai, bot, creature))
            {
                eligible = true;
                corpseGuid = loot.guid.GetRawValue();
                corpseEntry = creature->GetEntry();
                botDist = sServerFacade.GetDistance2d(bot, creature);
                masterDist = sServerFacade.GetDistance2d(master, creature);
            }
        }
    }

    group_quest_loot::Leash step = group_quest_loot::LeashStep(GroupQuestHold(ai), eligible, corpseGuid, botDist, masterDist,
        sPlayerbotAIConfig.lootDistance, int64(time(nullptr)));
    // one line at the start of a hold and one when it expires
    if (step == group_quest_loot::Leash::Start || step == group_quest_loot::Leash::Expire)
    {
        sLog.outBasic("[GroupQuestLoot] bot=%u corpse_guid=%llu entry=%u gate=leash result=%s bot_dist=%.1f master_dist=%.1f",
            bot->GetGUIDLow(), (unsigned long long)corpseGuid, corpseEntry, step == group_quest_loot::Leash::Start ? "hold" : "expired",
            botDist, masterDist);
    }
    // expired: the corpse takes the old rules from now on (GroupQuestLootCorpse is false for it), the loot target is reset, so
    // FarFromCurrentLootTrigger / LootAction / "move to loot" do not pull the bot back against "follow"
    if (step == group_quest_loot::Leash::Expire)
        context->GetValue<LootObject>("loot target")->Set(LootObject());
    return group_quest_loot::LeashQuiet(step);
}

bool LootAvailableTrigger::IsActive()
{
    return AI_VALUE(bool, "has available loot") &&
            (
                    sServerFacade.IsDistanceLessOrEqualThan(AI_VALUE2(float, "distance", "loot target"), INTERACTION_DISTANCE) ||
                    AI_VALUE(std::list<ObjectGuid>, "all targets").empty()
            ) &&
            !AI_VALUE2(bool, "combat", "self target") &&
            !AI_VALUE2(bool, "mounted", "self target");
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");

    if (!loot.IsLootPossible(bot))
        return false;

    // Abandon loot the bot cannot safely reach without breaking its follow leash.
    // "move to loot" runs at priority 7 but "out of free move range" fires "follow" at
    // ACTION_HIGH (20). A corpse is only reachable without oscillation if the master is
    // within followDistance + GetMaxLootDistance of it — otherwise the bot must leave the
    // leash to reach the corpse and follow immediately wins, causing a yo-yo.
    Player* master = ai->GetMaster();
    if (master && master != bot)
    {
        Creature* creature = ai->GetCreature(loot.guid);
        if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        {
            // twow-repo#780 (c) (Loot.GroupQuestLoot): a quest corpse is kept while the player is within LootDistance of it;
            // the leash (d) stays quiet for it. Key 0 or any other corpse: followDistance + loot distance as before.
            float safeRange = group_quest_loot::MasterBound(GroupQuestLootCorpse(ai, bot, creature), sPlayerbotAIConfig.followDistance,
                bot->GetMaxLootDistance(creature), sPlayerbotAIConfig.lootDistance);
            if (sServerFacade.GetDistance2d(master, creature) > safeRange)
                return false;
        }
    }

    // Must agree with OpenLootAction::DoLoot's loot-range rule, or the bot deadlocks: for a
    // creature corpse the server validates loot with a 3D distance check (Player::SendLoot),
    // but the plain "distance" value here is 2D and ignores Z. On sloped ground the bot read
    // "close enough" (2D <= 5) and so never fired "move to loot", yet "open loot" failed the
    // 3D gate -> it sat a few yards off a corpse it could not loot. Use the same 3D rule so
    // the bot keeps approaching until it is genuinely on the corpse, then loots.
    if (Creature* creature = GetBotAI(bot)->GetCreature(loot.guid))
    {
        if (sServerFacade.GetDeathState(creature) == CORPSE)
            return !creature->IsWithinDistInMap(bot, bot->GetMaxLootDistance(creature), true, SizeFactor::None);
    }

    return AI_VALUE2(float, "distance", "loot target") > INTERACTION_DISTANCE;
}

bool CanLootTrigger::IsActive()
{
    return AI_VALUE(bool, "can loot");
}

bool LootRollPendingTrigger::IsActive()
{
    // twow-repo#568 (D48): the same short-circuit order as AutoLootRollAction::isPossible; bag space stays there.
    return bot->GetGroup() && !AI_VALUE(LootRollMap, "active rolls").empty();
}
