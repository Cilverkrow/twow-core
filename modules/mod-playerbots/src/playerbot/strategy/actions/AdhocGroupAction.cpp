#include "playerbot/playerbot.h"
#include "AdhocGroupAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "Group/Group.h"

#include <algorithm>

using namespace ai;

namespace
{
uint32 ObjectiveCounter(Player* player, adhoc_group::ObjectiveKey const& key)
{
    QuestStatusMap& statusMap = player->getQuestStatusMap();
    auto const it = statusMap.find(key.questId);
    if (it == statusMap.end() || key.objective >= QUEST_OBJECTIVES_COUNT)
        return 0;
    return it->second.m_itemcount[key.objective] + it->second.m_creatureOrGOcount[key.objective];
}

bool ObjectiveDone(Player* player, adhoc_group::ObjectiveKey const& key)
{
    Quest const* quest = sObjectMgr.GetQuestTemplate(key.questId);
    QuestStatusMap& statusMap = player->getQuestStatusMap();
    auto const it = statusMap.find(key.questId);
    if (!quest || it == statusMap.end() || key.objective >= QUEST_OBJECTIVES_COUNT)
        return true;
    if (it->second.m_status == QUEST_STATUS_COMPLETE)
        return true;
    return it->second.m_itemcount[key.objective] >= quest->ReqItemCount[key.objective] &&
        it->second.m_creatureOrGOcount[key.objective] >= quest->ReqCreatureOrGOCount[key.objective];
}

void GroupLevels(Group* group, uint32& minLevel, uint32& maxLevel)
{
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->getSource())
        {
            minLevel = std::min(minLevel, member->GetLevel());
            maxLevel = std::max(maxLevel, member->GetLevel());
        }
}

bool AllRosterBots(Group* group)
{
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        PlayerbotAI* memberAi = member ? GetBotAI(member) : nullptr;
        if (!member || !memberAi || memberAi->IsRealPlayer() || !sRandomPlayerbotMgr.IsPersistentRosterMember(member->GetGUIDLow()))
            return false;
    }
    return true;
}
}

bool AdhocGroupAction::isUseful()
{
    return sPlayerbotAIConfig.botGroupsEnabled && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        !ai->HasRealPlayerMaster() && !bot->InBattleGround() && !bot->InBattleGroundQueue();
}

adhoc_group::ObjectiveKey AdhocGroupAction::CurrentObjective(Player* player) const
{
    adhoc_group::ObjectiveKey key;
    PlayerbotAI* playerAi = GetBotAI(player);
    if (!playerAi)
        return key;

    TravelTarget* target = playerAi->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    if (!target || !target->IsActiveForDeathAttribution())
        return key;

    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(target->GetDestination());
    if (!objective || player->GetQuestStatus(objective->GetQuestId()) != QUEST_STATUS_INCOMPLETE)
        return key;

    key.questId = objective->GetQuestId();
    key.objective = objective->GetObjective();
    return key;
}

bool AdhocGroupAction::IsCandidateBot(Player* player) const
{
    // Roster bot, no real player in charge, not in an instance, battleground or raid.
    PlayerbotAI* playerAi = GetBotAI(player);
    if (!playerAi || playerAi->IsRealPlayer() || playerAi->HasRealPlayerMaster())
        return false;
    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(player->GetGUIDLow()))
        return false;
    if (player->InBattleGround() || player->GetMap()->IsDungeon())
        return false;
    if (Group* group = player->GetGroup())
        if (group->IsRaidGroup() || group->isBGGroup())
            return false;
    return true;
}

void AdhocGroupAction::SendInvite(Player* inviter, Player* invitee) const
{
    // The existing group path (InviteToGroupAction::Invite): the invitee's AI
    // answers with "accept invitation".
    WorldPacket p;
    uint32 const rolesMask = 0;
    p << invitee->GetName();
    p << rolesMask;
    inviter->GetSession()->HandleGroupInviteOpcode(p);
}

void AdhocGroupAction::RegisterPending(Group* group, uint32 now)
{
    if (!pendingKey.IsValid())
        return;

    if (now - pendingSince > 60)
    {
        pendingKey = adhoc_group::ObjectiveKey();  // the invite was not answered
        return;
    }

    adhoc_group::Registry::Entry entry;
    if (adhoc_group::Groups().Find(group->GetId(), entry) || !AllRosterBots(group))
    {
        pendingKey = adhoc_group::ObjectiveKey();
        return;
    }

    adhoc_group::Groups().Register(group->GetId(), pendingKey, now);
    // Design section 4: free-for-all, so there is no roll delay on quest items.
    group->SetLootMethod(FREE_FOR_ALL);
    group->SendUpdate();

    if (sPlayerbotAIConfig.botGroupsDiagnostics)
        sLog.outBasic("[BotGroup] kind=adhoc event=register bot=%u group=%u quest=%u objective=%u members=%u",
            bot->GetGUIDLow(), group->GetId(), pendingKey.questId, uint32(pendingKey.objective), group->GetMembersCount());

    pendingKey = adhoc_group::ObjectiveKey();
}

bool AdhocGroupAction::CheckLeave(Group* group, uint32 now)
{
    adhoc_group::Registry::Entry entry;
    if (!adhoc_group::Groups().Find(group->GetId(), entry))
        return false;  // not an ad-hoc group: the other group rules apply

    if (trackedGroup != group->GetId())
    {
        trackedGroup = group->GetId();
        lastProgress = now;
        lastCounter = ObjectiveCounter(bot, entry.key);
        outOfRangeSince = 0;
        levelWindowSince = 0;
    }

    uint32 const counter = ObjectiveCounter(bot, entry.key);
    if (counter != lastCounter)
    {
        lastCounter = counter;
        lastProgress = now;
    }

    Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());
    float const radius = sPlayerbotAIConfig.botGroupsAdhocRadius;
    bool const outOfRange = leader && leader != bot &&
        (leader->GetMapId() != bot->GetMapId() || sServerFacade.GetDistance2d(bot, leader) > 2.0f * radius);
    outOfRangeSince = outOfRange ? (outOfRangeSince ? outOfRangeSince : now) : 0;

    uint32 minLevel = bot->GetLevel();
    uint32 maxLevel = bot->GetLevel();
    GroupLevels(group, minLevel, maxLevel);
    bool const outsideWindow = !adhoc_group::LevelWindowOk(minLevel, maxLevel, sPlayerbotAIConfig.botGroupsLevelWindow);
    levelWindowSince = outsideWindow ? (levelWindowSince ? levelWindowSince : now) : 0;

    adhoc_group::LeaveFacts facts;
    facts.questInProgress = bot->GetQuestStatus(entry.key.questId) == QUEST_STATUS_INCOMPLETE ||
        bot->GetQuestStatus(entry.key.questId) == QUEST_STATUS_COMPLETE;
    facts.objectiveDone = ObjectiveDone(bot, entry.key);
    facts.inInstance = bot->GetMap()->IsDungeon() || group->IsRaidGroup();
    facts.now = now;
    facts.outOfRangeSince = outOfRangeSince;
    facts.levelWindowSince = levelWindowSince;
    facts.lastProgress = lastProgress;

    adhoc_group::Leave const reason = adhoc_group::DecideLeave(facts);
    if (reason == adhoc_group::Leave::None)
        return false;

    if (sPlayerbotAIConfig.botGroupsDiagnostics)
        sLog.outBasic("[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u",
            bot->GetGUIDLow(), group->GetId(), entry.key.questId, uint32(entry.key.objective),
            adhoc_group::LeaveName(reason), group->GetMembersCount());

    // The group ends with the second-to-last member (Core disband).
    if (group->GetMembersCount() <= 2)
        adhoc_group::Groups().Forget(group->GetId());

    trackedGroup = 0;
    // The Core leave path (#301), with the bot itself as the issuer.
    ai->DoSpecificAction("leave", Event("adhoc group", "", bot), true);
    return true;
}

void AdhocGroupAction::Scan(uint32 now)
{
    Group* group = bot->GetGroup();
    adhoc_group::Registry::Entry entry;
    bool const inAdhoc = group && adhoc_group::Groups().Find(group->GetId(), entry);
    if (group && !inAdhoc)
        return;  // one ad-hoc group per bot; any other group is never a candidate

    bool const leadsAdhoc = inAdhoc && group->IsLeader(bot->GetObjectGuid());
    if (inAdhoc && !leadsAdhoc)
        return;  // the leader invites for its group
    if (inAdhoc && group->GetMembersCount() >= sPlayerbotAIConfig.botGroupsMaxBots)
        return;

    adhoc_group::ObjectiveKey const key = inAdhoc ? entry.key : CurrentObjective(bot);
    if (!key.IsValid() || !IsCandidateBot(bot))
        return;

    uint32 minLevel = bot->GetLevel();
    uint32 maxLevel = bot->GetLevel();
    if (group)
        GroupLevels(group, minLevel, maxLevel);

    uint32 const cooldown = sPlayerbotAIConfig.botGroupsAdhocPairCooldownSeconds;
    float const radius = sPlayerbotAIConfig.botGroupsAdhocRadius;

    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest friendly players"))
    {
        Player* other = sObjectMgr.GetPlayer(guid);
        if (!other || other == bot || other->GetTeam() != bot->GetTeam() || other->GetMapId() != bot->GetMapId())
            continue;
        if (sServerFacade.GetDistance2d(bot, other) > radius || !IsCandidateBot(other))
            continue;
        if (adhoc_group::PairCooldowns().IsBlocked(bot->GetGUIDLow(), other->GetGUIDLow(), now))
            continue;
        if (CurrentObjective(other) != key)
            continue;
        if (!adhoc_group::LevelWindowOk(std::min(minLevel, other->GetLevel()), std::max(maxLevel, other->GetLevel()),
                sPlayerbotAIConfig.botGroupsLevelWindow))
            continue;

        Group* otherGroup = other->GetGroup();
        adhoc_group::Registry::Entry otherEntry;
        bool const otherInAdhoc = otherGroup && adhoc_group::Groups().Find(otherGroup->GetId(), otherEntry);
        if (otherGroup && !otherInAdhoc)
            continue;
        bool const otherLeads = otherInAdhoc && otherGroup->IsLeader(other->GetObjectGuid());
        if (otherInAdhoc && (otherEntry.key != key || otherGroup->GetMembersCount() >= sPlayerbotAIConfig.botGroupsMaxBots))
            continue;
        if (otherInAdhoc)
        {
            uint32 otherMin = std::min(minLevel, other->GetLevel());
            uint32 otherMax = std::max(maxLevel, other->GetLevel());
            GroupLevels(otherGroup, otherMin, otherMax);
            if (!adhoc_group::LevelWindowOk(otherMin, otherMax, sPlayerbotAIConfig.botGroupsLevelWindow))
                continue;
        }

        adhoc_group::Invite const invite = adhoc_group::Decide(bot->GetGUIDLow(), group != nullptr, leadsAdhoc,
            other->GetGUIDLow(), otherGroup != nullptr, otherLeads);
        if (invite == adhoc_group::Invite::None)
            continue;

        // An invite, joined or not, puts the pair on the cooldown (design 3.3).
        adhoc_group::PairCooldowns().Block(bot->GetGUIDLow(), other->GetGUIDLow(), now + cooldown, now);

        if (invite == adhoc_group::Invite::AInvitesB)
            SendInvite(bot, other);
        else
            SendInvite(other, bot);

        // Whoever ends up in the new group registers it on its next scan.
        pendingKey = key;
        pendingSince = now;

        if (sPlayerbotAIConfig.botGroupsDiagnostics)
            sLog.outBasic("[BotGroup] kind=adhoc event=invite inviter=%u invitee=%u quest=%u objective=%u joining_group=%u",
                invite == adhoc_group::Invite::AInvitesB ? bot->GetGUIDLow() : other->GetGUIDLow(),
                invite == adhoc_group::Invite::AInvitesB ? other->GetGUIDLow() : bot->GetGUIDLow(),
                key.questId, uint32(key.objective), otherInAdhoc ? otherGroup->GetId() : (group ? group->GetId() : 0u));
        return;  // at most one invite per scan
    }
}

bool AdhocGroupAction::Execute(Event& /*event*/)
{
    uint32 const now = uint32(time(nullptr));

    if (Group* group = bot->GetGroup())
    {
        RegisterPending(group, now);
        if (CheckLeave(group, now))
            return true;
    }
    else
        trackedGroup = 0;

    if (now - lastScan < sPlayerbotAIConfig.botGroupsAdhocScanIntervalSeconds)
        return false;
    lastScan = now;

    Scan(now);
    return false;
}
