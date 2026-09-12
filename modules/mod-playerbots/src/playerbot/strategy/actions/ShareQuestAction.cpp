
#include "playerbot/playerbot.h"
#include "ShareQuestAction.h"

using namespace ai;

bool ShareQuestAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string link = event.getParam();

    if (!requester)
        return false;

    PlayerbotChatHandler handler(requester);
    uint32 entry = handler.extractQuestId(link);
    if (!entry)
        return false;

    Quest const* quest = sObjectMgr.GetQuestTemplate(entry);
    if (!quest)
        return false;

    // remove all quest entries for 'entry' from quest log
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 logQuest = bot->GetQuestSlotQuestId(slot);
        if (logQuest == entry)
        {
            WorldPacket p;
            p << entry;
            bot->GetSession()->HandlePushQuestToParty(p);
            ai->TellPlayer(requester, "Quest shared", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return true;
        }
    }

    return false;
}

bool CatchupQuestAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    Player* master = GetMaster();
    if (!master || requester != master || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return false;

    PlayerbotChatHandler handler(master);
    uint32 questId = handler.extractQuestId(event.getParam());
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    Group* group = bot->GetGroup();
    if (!quest || !questId || !master->IsCurrentQuest(questId) || !master->CanShareQuest(questId) ||
        !group || group != master->GetGroup() ||
        (group->IsRaidGroup() && !quest->HasQuestFlag(QUEST_FLAGS_RAID)) ||
        !bot->IsAtGroupRewardDistance(master) || master->GetLevel() < bot->GetLevel() ||
        master->GetLevel() - bot->GetLevel() > 8)
        return false;

    // Same command is a no-op; it must never alter existing progress.
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
        return true;

    if (!bot->CanTakeQuestForCatchup(quest, false) || !bot->CanAddQuest(quest, false))
        return false;

    bot->AddQuest(quest, nullptr);
    return bot->GetQuestStatus(questId) != QUEST_STATUS_NONE;
}

bool AutoShareQuestAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    bool shared = false;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 logQuest = bot->GetQuestSlotQuestId(slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(logQuest);

        if (!quest)
            continue;

        bool partyNeedsQuest = false;

        for (GroupReference* itr = bot->GetGroup()->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* player = itr->getSource();

            if (!player || player == bot || !player->IsInWorld() || !ai->IsSafe(player))         // skip self
                continue;

            if (bot->GetDistance(player) > 10)
                continue;

            if (!player->SatisfyQuestStatus(quest, false))
                continue;

            if (player->GetQuestStatus(logQuest) == QUEST_STATUS_COMPLETE)
                continue;

            if (!player->CanTakeQuest(quest, false))
                continue;

            if (!player->SatisfyQuestLog(false))
                continue;

            if (player->GetDividerGuid())
                continue;

            if (GetBotAI(player))
            {
                if (PAI_VALUE(uint8, "free quest log slots") < 15 || !urand(0,5))
                {
                    WorldPacket packet(CMSG_PUSHQUESTTOPARTY, 20);
                    packet << logQuest;
                    GetBotAI(player)->HandleMasterIncomingPacket(packet);
                }
            }
            else
                partyNeedsQuest = true;
        }

        if (!partyNeedsQuest)
            continue;

        WorldPacket p;
        p << logQuest;
        bot->GetSession()->HandlePushQuestToParty(p);
        ai->TellPlayer(requester, "Quest shared", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        shared = true;
    }

    return shared;
}
