#include "playerbot/playerbot.h"
#include "playerbot/ManagerFacts.h"
#include "playerbot/AdhocGroupWorld.h"
#include "playerbot/QuestGroupPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "Group/Group.h"

#include <algorithm>

// A3 step 6 T30-0: every function here runs on the bot's own map thread inside PlayerbotAI::PostManagerStatus and reads only
// that bot: its flags, its travel target, its quest log and its own group's header (id, leader guid, size, type). No member
// list, no other Player*, no other bot's AI (M6; the contract pins it).
namespace ai::manager_facts
{
namespace
{
TravelTarget* Target(PlayerbotAI* ai)
{
    return ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
}

// Taxi, teleport, a running S5a band trip or parked (design 3.1 b5; S7 skips the same bots, QuestGroupWorld.cpp Eligible and
// the band-trip test of its scan).
bool Busy(Player* bot, PlayerbotAI* ai)
{
    if (bot->IsTaxiFlying() || bot->IsBeingTeleported() || ai->IsParked())
        return true;
    TravelTarget* const target = Target(ai);
    return target && target->GetBandTripEnd() != 0;
}

// Open objectives of the bot's quest - the rule of QuestGroupWorld.cpp OpenObjectives (S7), for the bot alone (the contract
// pins the two expressions equal); dropOnly = some open objective is an item without a kill.
std::uint32_t OpenObjectives(Quest const* q, QuestStatusData const* st, bool& dropOnly)
{
    if (!q || !st || st->m_rewarded || st->m_status != QUEST_STATUS_INCOMPLETE)
        return 0;
    std::uint32_t open = 0;
    for (std::uint8_t i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (st->m_itemcount[i] < q->ReqItemCount[i] || st->m_creatureOrGOcount[i] < q->ReqCreatureOrGOCount[i])
        {
            ++open;
            if (q->ReqItemCount[i] && !q->ReqCreatureOrGOCount[i])
                dropOnly = true;
        }
    return open;
}
}

manager_inbox::StatusFlags ReadFlags(Player* bot, PlayerbotAI* ai)
{
    manager_inbox::StatusFlags f;
    f.alliance = bot->GetTeam() == ALLIANCE;
    f.alive = bot->IsAlive();
    // T30-1 splits the ownership test off (CandidateFacts, G2); today the whole rule is the candidate fact.
    f.candidate = adhoc_group::IsCandidateBot(bot);
    f.inBgQueue = bot->InBattleGroundQueue();
    f.dungeonMap = bot->GetMap()->IsDungeon();
    f.busy = Busy(bot, ai);
    f.startZoneHold = ai::zone_keep::StartZoneHolds(bot, ai);
    return f;
}

manager_inbox::KeyFacts ReadKey(Player* bot)
{
    manager_inbox::KeyFacts k;
    adhoc_group::ObjectiveKey const key = adhoc_group::CurrentObjective(bot);
    if (!key.IsValid())
        return k;
    k.questId = key.questId;
    k.objective = std::int8_t(key.objective);
    QuestStatusData const* st = bot->GetQuestStatusData(key.questId);
    if (!st)
        k.questStatus = manager_inbox::kQuestNone;
    else if (st->m_rewarded)
        k.questStatus = manager_inbox::kQuestRewarded;
    else if (st->m_status == QUEST_STATUS_COMPLETE)
        k.questStatus = manager_inbox::kQuestComplete;
    else if (st->m_status == QUEST_STATUS_INCOMPLETE)
        k.questStatus = manager_inbox::kQuestIncomplete;
    k.objDone = adhoc_group::ObjectiveDone(bot, key);
    k.progress = adhoc_group::ObjectiveCounter(bot, key);
    return k;
}

manager_inbox::EdgeFacts ReadEdge(Player* bot)
{
    manager_inbox::EdgeFacts e;
    e.invite = bot->GetGroupInvite() != nullptr;
    Group* const group = bot->GetGroup();
    if (!group)
        return e;
    e.groupId = group->GetId();
    e.leaderLow = group->GetLeaderGuid().GetCounter();
    e.members = group->GetMembersCount();
    e.leader = group->IsLeader(bot->GetObjectGuid());
    e.raid = group->IsRaidGroup();
    e.bgGroup = group->isBGGroup();
    return e;
}

bool ReadWish(Player* bot, PlayerbotAI* ai, manager_inbox::WishFacts& out)
{
    // The S7 scan's bot rule (QuestGroupWorld.cpp Eligible + "no group" + the band trip), plus parked (design 3.1 busy).
    if (!bot->IsAlive() || bot->GetGroup() || bot->InBattleGround() || bot->InBattleGroundQueue() || bot->GetMap()->IsDungeon() ||
        Busy(bot, ai))
        return false;
    TravelTarget* const target = Target(ai);
    std::uint32_t bestQuest = 0;
    std::uint32_t bestOpen = 0;
    bool bestDrop = false;
    for (std::uint16_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        std::uint32_t const questId = bot->GetQuestSlotQuestId(slot);
        Quest const* q = questId ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
        if (!q || !quest_groups::IsGroupHelpQuest(q->GetType(), q->GetSuggestedPlayers(), sPlayerbotAIConfig.questGroupsSuggestedPlayers))
            continue;
        QuestStatusData const* st = bot->GetQuestStatusData(questId);
        bool drop = false;
        quest_groups::QuestFacts f;
        f.incomplete = st && !st->m_rewarded && st->m_status == QUEST_STATUS_INCOMPLETE;
        f.classQuest = q->GetRequiredClasses() != 0;
        f.botLevel = bot->GetLevel();
        f.questLevel = q->GetQuestLevel() > 0 ? std::uint32_t(q->GetQuestLevel()) : f.botLevel;
        f.abandoned = target && target->IsStructurallyAbandoned(questId);
        f.openObjectives = OpenObjectives(q, st, drop);
        // One wish per period: the waiting quest with the most open objectives (F3 (a)); ties keep the earlier log slot.
        if (!quest_groups::Waits(f) || f.openObjectives <= bestOpen)
            continue;
        bestQuest = questId;
        bestOpen = f.openObjectives;
        bestDrop = drop;
    }
    if (!bestQuest)
        return false;
    out = manager_inbox::WishFacts();
    out.questId = bestQuest;
    out.openObjectives = bestOpen;
    out.dropOnly = bestDrop;
    out.level = std::uint8_t(std::min<std::uint32_t>(255, bot->GetLevel()));
    out.mapId = bot->GetMapId();
    out.x = bot->GetPositionX();
    out.y = bot->GetPositionY();
    out.guildId = bot->GetGuildId();
    return true;
}
}
