#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/BotGroupDiagnostics.h"
#include "playerbot/BotGroupPolicy.h"
#include "Group/Group.h"

#include <algorithm>
#include <set>

namespace ai::bot_group
{
namespace
{
std::set<uint32> QuestLog(Player* player)
{
    std::set<uint32> quests;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (uint32 questId = player->GetQuestSlotQuestId(slot))
            quests.insert(questId);
    return quests;
}
}

void LogMembership(Player* bot, Group* group, char const* event)
{
    if (!sPlayerbotAIConfig.botGroupsDiagnostics || !bot || !group || group->isBGGroup())
        return;

    if (!sRandomPlayerbotMgr.IsFreeBot(bot))
        return;

    Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());
    PlayerbotAI* leaderAi = GetBotAI(leader);
    bool const leaderIsRealPlayer = leader && (!leaderAi || leaderAi->IsRealPlayer());

    unsigned botMembers = 0;
    uint32 minLevel = bot->GetLevel();
    uint32 maxLevel = bot->GetLevel();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (!member)
            continue;
        PlayerbotAI* memberAi = GetBotAI(member);
        if (memberAi && !memberAi->IsRealPlayer())
            ++botMembers;
        minLevel = std::min(minLevel, member->GetLevel());
        maxLevel = std::max(maxLevel, member->GetLevel());
    }

    Verdict const verdict = Classify(leaderIsRealPlayer, botMembers, maxLevel - minLevel,
        sPlayerbotAIConfig.botGroupsMaxBots, sPlayerbotAIConfig.botGroupsLevelWindow);

    std::set<uint32> const botQuests = QuestLog(bot);
    unsigned sharedQuests = 0;
    unsigned leaderQuests = 0;
    if (leader && leader != bot)
    {
        std::set<uint32> const leaderLog = QuestLog(leader);
        leaderQuests = unsigned(leaderLog.size());
        for (uint32 questId : leaderLog)
            if (botQuests.count(questId))
                ++sharedQuests;
    }

    sLog.outBasic("[BotGroup] event=%s bot=%u leader=%u group=%u roster=%u leader_real=%u members=%u bots=%u "
        "level_spread=%u verdict=%s enabled=%u max_bots=%u level_window=%u bot_quests=%u leader_quests=%u shared_quests=%u",
        event, bot->GetGUIDLow(), leader ? leader->GetGUIDLow() : 0u, group->GetId(),
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) ? 1u : 0u,
        leaderIsRealPlayer ? 1u : 0u, group->GetMembersCount(), botMembers, maxLevel - minLevel,
        VerdictName(verdict), sPlayerbotAIConfig.botGroupsEnabled ? 1u : 0u,
        sPlayerbotAIConfig.botGroupsMaxBots, sPlayerbotAIConfig.botGroupsLevelWindow,
        unsigned(botQuests.size()), leaderQuests, sharedQuests);
}
}
