
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
    if (!requester)
        return false;

    PlayerbotChatHandler handler(requester);
    uint32 questId = handler.extractQuestId(event.getParam());

    CatchupResult const result = Admit(bot, master, requester, questId);
    sLog.outBasic("[QuestShare] path=chat bot=%u master=%u quest=%u result=%s reason=%s",
        bot->GetGUIDLow(), master ? master->GetGUIDLow() : 0, questId,
        result == CatchupResult::ADMITTED || result == CatchupResult::ALREADY_HAS ? "admitted" : "rejected",
        ResultCode(result));

    if (result == CatchupResult::ADMITTED)
    {
        ai->TellPlayer(requester, BOT_TEXT("quest_accept"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    // Same command is a no-op; it must never alter existing progress.
    if (result == CatchupResult::ALREADY_HAS)
        return true;

    ai->TellError(requester, std::string("Cannot catch up on this quest: ") + ResultCode(result));
    return false;
}

CatchupResult CatchupQuestAction::Admit(Player* bot, Player* master, Player* requester, uint32 questId)
{
    if (!master || requester != master)
        return CatchupResult::NOT_MASTER;
    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return CatchupResult::NOT_ROSTER;

    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (!quest || !questId)
        return CatchupResult::NO_QUEST;
    if (!master->IsCurrentQuest(questId))
        return CatchupResult::MASTER_LACKS_QUEST;
    if (!master->CanShareQuest(questId))
        return CatchupResult::NOT_SHAREABLE;

    Group* group = bot->GetGroup();
    if (!group || group != master->GetGroup())
        return CatchupResult::NOT_SAME_GROUP;
    if (group->IsRaidGroup() && !quest->HasQuestFlag(QUEST_FLAGS_RAID))
        return CatchupResult::RAID;
    if (!bot->IsAtGroupRewardDistance(master))
        return CatchupResult::TOO_FAR;
    if (master->GetLevel() < bot->GetLevel() || master->GetLevel() - bot->GetLevel() > 8)
        return CatchupResult::LEVEL_WINDOW;

    // Never alter existing progress.
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
        return CatchupResult::ALREADY_HAS;

    if (!bot->CanTakeQuestForCatchup(quest, false))
        return CatchupResult::CANT_TAKE;
    if (!bot->CanAddQuest(quest, false))
        return CatchupResult::LOG_FULL;

    bot->AddQuest(quest, nullptr);
    return bot->GetQuestStatus(questId) != QUEST_STATUS_NONE ? CatchupResult::ADMITTED : CatchupResult::CANT_TAKE;
}

char const* CatchupQuestAction::ResultCode(CatchupResult result)
{
    switch (result)
    {
        case CatchupResult::ADMITTED:           return "ok";
        case CatchupResult::ALREADY_HAS:        return "already_has";
        case CatchupResult::NOT_MASTER:         return "not_master";
        case CatchupResult::NOT_ROSTER:         return "not_roster";
        case CatchupResult::NO_QUEST:           return "no_quest";
        case CatchupResult::MASTER_LACKS_QUEST: return "master_lacks_quest";
        case CatchupResult::NOT_SHAREABLE:      return "not_shareable";
        case CatchupResult::NOT_SAME_GROUP:     return "not_same_group";
        case CatchupResult::RAID:               return "raid";
        case CatchupResult::TOO_FAR:            return "too_far";
        case CatchupResult::LEVEL_WINDOW:       return "level_window";
        case CatchupResult::CANT_TAKE:          return "cant_take";
        case CatchupResult::LOG_FULL:           return "log_full";
    }
    return "cant_take";
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
