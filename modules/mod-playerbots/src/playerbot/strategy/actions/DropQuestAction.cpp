
#include "playerbot/playerbot.h"
#include "DropQuestAction.h"
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Formulas.h"

using namespace ai;

bool DropQuestAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string link = event.getParam();
    if (!GetMaster())
        return false;

    PlayerbotChatHandler handler(GetMaster());
    uint32 entry = handler.extractQuestId(link);
    bool dropped = false;

    // remove all quest entries for 'entry' from quest log
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 logQuest = bot->GetQuestSlotQuestId(slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(logQuest);
        if (!quest)
            continue;

        if (logQuest == entry || link.find(quest->GetTitle()) != std::string::npos || link == "all")
        {
            bot->SetQuestSlot(slot, 0);

            // we ignore unequippable quest items in this case, its' still be equipped
            bot->TakeQuestSourceItem(logQuest, false);
            entry = logQuest;

            bot->SetQuestStatus(entry, QUEST_STATUS_NONE);
            bot->getQuestStatusMap()[entry].m_rewarded = false;

            dropped = true;

            if (link != "all")
                break;
        }
    }

    if(dropped)
        ai->TellPlayer(requester, BOT_TEXT("quest_remove"));

    return dropped;
}

// Hotfix 8.5: a roster bot on its own drops open grey quests whatever the log size.
static void DropGreyQuestsOnItsOwn(PlayerbotAI* ai, Player* bot)
{
    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || ai->HasRealPlayerMaster())
        return;

    // Hotfix 8.7: the XP grey level, not Quests.LowLevelHideDiff (-1 on this server).
    uint32 const grayLevel = MaNGOS::XP::GetGrayLevel(bot->GetLevel());
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        Quest const* quest = questId ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
        if (!quest)
            continue;

        if (ai::quest_search::DropGreyQuest(true, bot->GetQuestLevelForPlayer(quest), grayLevel,
                bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE, quest->GetRequiredClasses() != 0))
            ai->DropQuest(questId, "grey");
    }
}

bool CleanQuestLogAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string link = event.getParam();
    if (ai->HasActivePlayerMaster())
        return false;

    DropGreyQuestsOnItsOwn(ai, bot);

    uint8 totalQuests = 0;

    DropQuestType(requester, totalQuests); //Count the total quests
     
    if (MAX_QUEST_LOG_SIZE - totalQuests > 6)
    {
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE, true, true); //Drop failed quests
        return true;
    }

    if (AI_VALUE(bool, "can fight equal")) //Only drop gray quests when able to fight proper lvl quests.
    {
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 6); //Drop gray/red quests.
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 6, false, true); //Drop gray/red quests with progress.
        DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 6, false, true, true); //Drop gray/red completed quests.
    }

    if (MAX_QUEST_LOG_SIZE - totalQuests > 4)
        return true;

    DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 4, true); //Drop quests without progress.

    if (MAX_QUEST_LOG_SIZE - totalQuests > 2)
        return true;

    DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 2, true, true); //Drop quests with progress.

    if (MAX_QUEST_LOG_SIZE - totalQuests > 0)
        return true;

    DropQuestType(requester, totalQuests, MAX_QUEST_LOG_SIZE - 1, true, true, true); //Drop completed quests.

    if (MAX_QUEST_LOG_SIZE - totalQuests > 0)
        return true;

    return false;
}

void CleanQuestLogAction::DropQuestType(Player* requester, uint8 &numQuest, uint8 wantNum, bool isGreen, bool hasProgress, bool isComplete)
{
    std::vector<uint8> slots;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        slots.push_back(slot);


    if (wantNum < 100)
    {
        std::shuffle(slots.begin(), slots.end(), *GetRandomGenerator());
    }

    for (uint8 slot : slots)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);

        if (!questId)
            continue;

        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_FAILED)
        {
            if (quest->GetRequiredClasses()) //Do not drop class specific quests
                continue;

            if (wantNum == 100)
                numQuest++;

            int32 lowLevelDiff = sWorld.getConfig(CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF);
            if (lowLevelDiff < 0 || bot->GetLevel() <= bot->GetQuestLevelForPlayer(quest) + uint32(lowLevelDiff)) //Quest is not gray
            {
                if (bot->GetLevel() + 5 > bot->GetQuestLevelForPlayer(quest)) //Quest is not red
                    if (!isGreen)
                        continue;
            }
            else //Quest is gray
            {
                if (isGreen)
                    continue;
            }

            if (HasProgress(bot, quest) && !hasProgress && bot->GetQuestStatus(questId) != QUEST_STATUS_FAILED)
                continue;

            if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                continue;

            if (numQuest <= wantNum)
                continue;
        }

        //Drop quest.
        GetBotAI(bot)->DropQuest(questId, bot->GetQuestStatus(questId) == QUEST_STATUS_FAILED ? "clean_failed" :
            isComplete ? "clean_complete" : hasProgress ? "clean_progress" : isGreen ? "clean_no_progress" : "clean_grey_red");

        numQuest--;

        ai->TellPlayer(requester, BOT_TEXT("quest_remove") + " " + chat->formatQuest(quest), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}

bool CleanQuestLogAction::HasProgress(Player* bot, Quest const* quest)
{
    uint32 questId = quest->GetQuestId();

    if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        return true;

    QuestStatusData questStatus = bot->getQuestStatusMap()[questId];

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        if (!quest->ObjectiveText[i].empty())
            return true;

        if (quest->ReqItemId[i])
        {
            int required = quest->ReqItemCount[i];
            int available = questStatus.m_itemcount[i];
            if (available > 0 && required > 0)
                return true;
        }

        if (quest->ReqCreatureOrGOId[i])
        {
            int required = quest->ReqCreatureOrGOCount[i];
            int available = questStatus.m_creatureOrGOcount[i];

            if (available > 0 && required > 0)
                return true;
        }
    }

    return false;
}