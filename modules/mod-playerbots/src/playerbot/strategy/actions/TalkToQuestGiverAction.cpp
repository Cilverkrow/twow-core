#include "Config/Config.h"

#include "playerbot/playerbot.h"
#include "TalkToQuestGiverAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/LootRolePolicy.h"

using namespace ai;

bool TalkToQuestGiverAction::ProcessQuest(Player* requester, Quest const* quest, WorldObject* questGiver)
{
    bool isCompleted = false;

    std::string outputMessage;
    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);

    QuestStatus status = bot->GetQuestStatus(quest->GetQuestId());
    if (sPlayerbotAIConfig.syncQuestForPlayer)
    {
        if (requester && (!GetBotAI(requester) || GetBotAI(requester)->IsRealPlayer()))
        {
            QuestStatus masterStatus = requester->GetQuestStatus(quest->GetQuestId());
            if (masterStatus == QUEST_STATUS_INCOMPLETE || masterStatus == QUEST_STATUS_FAILED)
            {
                isCompleted |= CompleteQuest(requester, quest->GetQuestId());
            }
        }
    }

    if (sPlayerbotAIConfig.syncQuestWithPlayer)
    {        
        if (requester && requester->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_COMPLETE && (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_FAILED))
        {
            isCompleted |= CompleteQuest(bot, quest->GetQuestId());
            status = bot->GetQuestStatus(quest->GetQuestId());
        }
    }    

    switch (status)
    {
        case QUEST_STATUS_COMPLETE:
        {
            isCompleted |= TurnInQuest(requester, quest, questGiver, outputMessage);
            break;
        }
        case QUEST_STATUS_INCOMPLETE:
        {
            outputMessage = BOT_TEXT2("quest_status_incomplete", args);
            break;
        }
        case QUEST_STATUS_AVAILABLE:
        case QUEST_STATUS_NONE:
        {
            if (quest->IsAutoComplete() && bot->CanTakeQuest(quest, false) && bot->CanRewardQuest(quest, false))
                isCompleted |= TurnInQuest(requester, quest, questGiver, outputMessage);
            else
                outputMessage = BOT_TEXT2("quest_status_available", args);
            break;
        }
        case QUEST_STATUS_FAILED:
        {
            outputMessage = BOT_TEXT2("quest_status_failed", args);
            break;
        }
    }

    ai->TellPlayer(requester, outputMessage, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    return isCompleted;
}

bool TalkToQuestGiverAction::TurnInQuest(Player* requester, Quest const* quest, WorldObject* questGiver, std::string& out)
{
    uint32 questID = quest->GetQuestId();
    if (bot->GetQuestRewardStatus(questID))
    {
        return false;
    }
    
    if (sPlayerbotAIConfig.globalSoundEffects)
    {
        bot->PlayDistanceSound(621);
    }

    sPlayerbotAIConfig.logEvent(ai, "TalkToQuestGiverAction", quest->GetTitle(), std::to_string(quest->GetQuestId()));

    if (quest->GetRewChoiceItemsCount() == 0)
    {
        RewardNoItem(quest, questGiver, out);
    }
    else if (quest->GetRewChoiceItemsCount() == 1)
    {
        RewardSingleItem(quest, questGiver, out);
    }
    else 
    {
        RewardMultipleItem(requester, quest, questGiver, out);
    }

    if(quest->GetRewChoiceItemsCount() || quest->GetRewItemsCount())
        ai->DoSpecificAction("equip upgrades");

    return true;
}

void TalkToQuestGiverAction::RewardNoItem(Quest const* quest, WorldObject* questGiver, std::string& out) 
{
    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);

    if (bot->CanRewardQuest(quest, false))
    {
        bot->RewardQuest(quest, 0, questGiver, false);
        out = BOT_TEXT2("quest_status_completed", args);

        BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
    }
    else
    {
        out = BOT_TEXT2("quest_status_unable_to_complete", args);
    }
}

void TalkToQuestGiverAction::RewardSingleItem(Quest const* quest, WorldObject* questGiver, std::string& out)
{
    int index = 0;
    ItemPrototype const *item = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[index]);

    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);
    args["%item"] = chat->formatItem(item);

    if (bot->CanRewardQuest(quest, index, false))
    {
        bot->RewardQuest(quest, index, questGiver, true);
        out = BOT_TEXT2("quest_status_complete_single_reward", args);

        BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
    }
    else
    {
        out = BOT_TEXT2("quest_status_unable_to_complete", args);
    }
}

ItemIds TalkToQuestGiverAction::BestRewards(Quest const* quest)
{
    ItemIds returnIds;
    ItemUsage bestUsage = ItemUsage::ITEM_USAGE_NONE;
    if (quest->GetRewChoiceItemsCount() == 0)
    {
        return returnIds;
    }
    else if (quest->GetRewChoiceItemsCount() == 1)
    {
        return { 0 };
    }
    else
    {
        uint32 guildShareRewardItemId = AI_VALUE(uint32, "guild share quest reward item");
        if (guildShareRewardItemId)
        {
            for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
            {
                if (quest->RewChoiceItemId[i] == guildShareRewardItemId)
                {
                    return { i };
                }
            }
        }

        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]);
            if (usage == ItemUsage::ITEM_USAGE_EQUIP)
            {
                bestUsage = ItemUsage::ITEM_USAGE_EQUIP;
            }
            else if (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP && bestUsage != ItemUsage::ITEM_USAGE_EQUIP)
            {
                bestUsage = usage;
            }
            else if (usage != ItemUsage::ITEM_USAGE_NONE && bestUsage == ItemUsage::ITEM_USAGE_NONE)
            {
                bestUsage = usage;
            }
        }
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]);
            if (usage == bestUsage)
            {
                returnIds.insert(i);
            }
        }
        return returnIds;
    }
}

// twow-repo#308 LOOT-ROLE-01: every reward the bot can equip is scored with the weight scale its role asks for (the
// premade path's): its weight minus the weight of what it would replace. RoleWeights on takes the best, off the first
// as before. [QuestReward] is always on, so picked != best counts today's wrong choices before the switch goes on.
uint32 TalkToQuestGiverAction::PickReward(Quest const* quest, ItemIds const& bestIds)
{
    uint8 const first = uint8(*bestIds.begin());
    uint32 const roleSpec = sRandomItemMgr.GetRoleSpecId(bot);
    std::vector<ai::loot_role::Choice> choices;
    uint32 equip = 0;
    for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
    {
        ai::loot_role::Choice choice;
        choice.index = i;
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[i]);
        if (proto && roleSpec && AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]) == ItemUsage::ITEM_USAGE_EQUIP)
        {
            choice.equip = true;
            ++equip;
            choice.quality = proto->Quality;
            choice.itemLevel = proto->ItemLevel;
            ItemSpecType spec = ITEM_SPEC_NONE;
            choice.gain = int64(sRandomItemMgr.CalculateStatWeight(bot->getClass(), roleSpec, proto, spec));
            uint16 dest = 0;
            if (RandomPlayerbotMgr::CanEquipUnseenItem(bot, NULL_SLOT, dest, proto->ItemId) == EQUIP_ERR_OK)
            {
                if (Item* old = bot->GetItemByPos(dest))
                {
                    ItemSpecType oldSpec = ITEM_SPEC_NONE;
                    choice.gain -= int64(sRandomItemMgr.CalculateStatWeight(bot->getClass(), roleSpec, old->GetProto(), oldSpec));
                }
            }
        }
        choices.push_back(choice);
    }

    int const best = ai::loot_role::BestByGain(choices);
    uint8 const picked = ai::loot_role::Pick(sPlayerbotAIConfig.lootRoleWeights, first, best);
    sLog.outBasic("[QuestReward] bot=%u class=%u level=%u spec=%u role_spec=%u quest=%u choices=%u equip=%u first=%u best=%d picked=%u wrong=%u reason=%s",
        bot->GetGUIDLow(), uint32(bot->getClass()), bot->GetLevel(), sRandomItemMgr.GetPlayerSpecId(bot), roleSpec,
        quest->GetQuestId(), uint32(choices.size()), equip, uint32(first), best, uint32(picked), uint32(best >= 0 && best != int(first)),
        picked == first ? "first" : "weight");
    return picked;
}

void TalkToQuestGiverAction::RewardMultipleItem(Player* requester, Quest const* quest, WorldObject* questGiver, std::string& out)
{
    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);

    std::set<uint32> bestIds;
    std::ostringstream outid;
    auto questRewardOption = static_cast<QuestRewardOptionType>(AI_VALUE(uint8, "quest reward"));
    if (!ai->IsAlt() ||
        (questRewardOption == QuestRewardOptionType::QUEST_REWARD_CONFIG_DRIVEN && sPlayerbotAIConfig.autoPickReward == "yes") ||
        questRewardOption == QuestRewardOptionType::QUEST_REWARD_OPTION_AUTO
    ) {
        //Pick the first item of the best rewards (#308 RoleWeights: the one with the largest gain).
        bestIds = BestRewards(quest);
        uint32 const rewardIndex = PickReward(quest, bestIds);
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[rewardIndex]);
        if(proto)
        {
            args["%item"] = chat->formatItem(proto);
            out = BOT_TEXT2("quest_status_complete_single_reward", args);

            BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
        }

        bot->RewardQuest(quest, rewardIndex, questGiver, true);
    }
    else if ((questRewardOption == QuestRewardOptionType::QUEST_REWARD_CONFIG_DRIVEN && sPlayerbotAIConfig.autoPickReward == "no") ||
             questRewardOption == QuestRewardOptionType::QUEST_REWARD_OPTION_LIST
    ) {
        // Old functionality, list rewards.
        AskToSelectReward(requester, quest, out, false);
    }
    else
    {
        // Try to pick the usable item. If multiple, list usable rewards.
        bestIds = BestRewards(quest);
        if (bestIds.size() > 1)
        {
            AskToSelectReward(requester, quest, out, true);
        }
        else
        {
            uint32 rewardIndex = bestIds.empty() ? 0 : *bestIds.begin();
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[rewardIndex]);
            if (proto)
            {
                args["%item"] = chat->formatItem(proto);
                out = BOT_TEXT2("quest_status_complete_single_reward", args);

                BroadcastHelper::BroadcastQuestTurnedIn(ai, bot, quest);
            }

            bot->RewardQuest(quest, rewardIndex, questGiver, true);
        }
    }
}

void TalkToQuestGiverAction::AskToSelectReward(Player* requester, Quest const* quest, std::string& out, bool forEquip)
{
    std::ostringstream msg;
    for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
    {
        ItemPrototype const* item = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[i]);
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewChoiceItemId[i]);

        if (!forEquip || BestRewards(quest).count(i) > 0)
        {
            msg << "\n" << chat->formatItem(item);
        }
    }

    std::map<std::string, std::string> args;
    args["%quest"] = chat->formatQuest(quest);
    args["%rewards"] = msg.str();

    out = BOT_TEXT2("quest_status_complete_pick_reward", args);
}