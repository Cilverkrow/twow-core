#include "playerbot/playerbot.h"
#include "AdhocGroupAction.h"
#include "playerbot/AdhocGroupWorld.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/GroupOps.h"   // BOT-12: BotGroups.OpQueue
#include "Group/Group.h"

#include <algorithm>

using namespace ai;
using ai::adhoc_group::AllRosterBots;
using ai::adhoc_group::CurrentObjective;
using ai::adhoc_group::GroupLevels;
using ai::adhoc_group::IsCandidateBot;
using ai::adhoc_group::ObjectiveCounter;
using ai::adhoc_group::ObjectiveDone;

bool AdhocGroupAction::isUseful()
{
    // twow-repo#628: with AdHoc.WorldThread = 1 the world-thread manager forms and
    // leaves the ad-hoc groups; this map-thread action then does nothing.
    return sPlayerbotAIConfig.botGroupsEnabled && !sPlayerbotAIConfig.botGroupsAdhocWorldThread &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        !ai->HasRealPlayerMaster() && !bot->InBattleGround() && !bot->InBattleGroundQueue();
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
        leaderOtherMapSince = 0;
    }

    uint32 const counter = ObjectiveCounter(bot, entry.key);
    if (counter != lastCounter)
    {
        lastCounter = counter;
        lastProgress = now;
    }

    Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());
    float const radius = sPlayerbotAIConfig.botGroupsAdhocRadius;
    // BOT-12 (AiPlayerbot.BotGroups.LeaderOtherMap): with the rule on for the bot, a leader on another map (or dungeon
    // instance) runs its own clock and OutOfRange keeps the distance on the same map. Off: the old expression.
    bool const otherMapRule = ai::cohort::Applies(sPlayerbotAIConfig.botGroupsLeaderOtherMap, bot->GetGUIDLow());
    bool const otherMap = leader && leader != bot && adhoc_group::OnOtherMap(leader->GetMapId() != bot->GetMapId(),
        otherMapRule && leader->GetInstanceId() != bot->GetInstanceId(), bot->GetMap()->Instanceable());
    bool const outOfRange = leader && leader != bot && adhoc_group::OutOfRangeFact(otherMap,
        !otherMap && sServerFacade.GetDistance2d(bot, leader) > 2.0f * radius, otherMapRule);
    outOfRangeSince = outOfRange ? (outOfRangeSince ? outOfRangeSince : now) : 0;
    leaderOtherMapSince = adhoc_group::LeaderOtherMapClock(otherMapRule, leader != nullptr, otherMap, leaderOtherMapSince, now);

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
    facts.leaderOtherMapSince = leaderOtherMapSince;
    facts.leaderOtherMapSeconds = otherMapRule ? sPlayerbotAIConfig.botGroupsLeaderOtherMapSeconds : 0;

    adhoc_group::Leave const reason = adhoc_group::DecideLeave(facts);
    if (reason == adhoc_group::Leave::None)
        return false;

    // BOT-12 (twow-repo#677, AiPlayerbot.BotGroups.OpQueue): every decided leave - leader_other_map included - goes to the
    // world-thread queue; no group change on this map thread.
    if (group_ops::QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, bot->GetGUIDLow()))
        return EnqueueLeave(group, entry, reason, now);

    if (sPlayerbotAIConfig.botGroupsDiagnostics)
        sLog.outBasic("[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u",
            bot->GetGUIDLow(), group->GetId(), entry.key.questId, uint32(entry.key.objective),
            adhoc_group::LeaveName(reason), group->GetMembersCount());

    // The group ends with the second-to-last member (Core disband).
    if (group->GetMembersCount() <= 2)
        adhoc_group::Groups().Forget(group->GetId());

    trackedGroup = 0;
    // The Core leave path (#301), with the bot itself as the issuer. A free (roster) bot stays in its group here
    // (ShouldStayInGroup, twow-repo#628 third finding); only the world-thread queue (BOT-12, OpQueue) really leaves.
    ai->DoSpecificAction("leave", Event("adhoc group", "", bot), true);
    return true;
}

// BOT-12 (twow-repo#677, AiPlayerbot.BotGroups.OpQueue): the decided leave as a world-thread op. The clocks stay: trackedGroup
// is reset only when the bot is out of that group (Execute / the next CheckLeave), so a dropped op (pair or group changed,
// transit) is decided again at once, and nothing repeats while the op waits (Execute returns while it is pending).
bool AdhocGroupAction::EnqueueLeave(Group* group, adhoc_group::Registry::Entry const& entry, adhoc_group::Leave reason, uint32 now)
{
    if (now < opRetryAt)
        return false;   // the queue refused the last op: decide again after one scan interval

    group_ops::Op op;
    op.kind = group_ops::OpKind::Leave;
    op.reason = uint8(reason);
    op.source = uint8(group_ops::OpSource::AdhocAction);
    op.bot = bot->GetGUIDLow();
    op.other = group->GetLeaderGuid().GetCounter();
    op.groupId = group->GetId();
    op.questId = entry.key.questId;
    op.objective = entry.key.objective;
    if (group_ops::Enqueue(op, ai, nullptr) != group_ops::Pushed::Queued)
    {
        opRetryAt = now + std::max<uint32>(1, sPlayerbotAIConfig.botGroupsAdhocScanIntervalSeconds);
        return false;
    }

    if (sPlayerbotAIConfig.botGroupsDiagnostics)
        sLog.outBasic("[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u via=queue",
            bot->GetGUIDLow(), group->GetId(), entry.key.questId, uint32(entry.key.objective),
            adhoc_group::LeaveName(reason), group->GetMembersCount());
    return true;
}

// BOT-12 (OpQueue): the invite as a world-thread op - the inviter's invite opcode and the invitee's accept run there; the join
// registers the ad-hoc group there too (no pendingKey here).
bool AdhocGroupAction::EnqueueJoin(Player* inviter, Player* invitee, adhoc_group::ObjectiveKey const& key)
{
    Group* inviterGroup = inviter->GetGroup();
    group_ops::Op op;
    op.kind = group_ops::OpKind::Join;
    op.source = uint8(group_ops::OpSource::AdhocAction);
    op.bot = invitee->GetGUIDLow();
    op.other = inviter->GetGUIDLow();
    op.groupId = inviterGroup ? inviterGroup->GetId() : 0u;
    op.questId = key.questId;
    op.objective = key.objective;
    return group_ops::Enqueue(op, GetBotAI(invitee), GetBotAI(inviter)) == group_ops::Pushed::Queued;
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
        // BOT-12 (OpQueue): a neighbour with an op on the world thread is no candidate until it ran (never set with OpQueue 0).
        if (PlayerbotAI* otherAi = GetBotAI(other))
            if (group_ops::StillPending(otherAi, other->GetGUIDLow()))
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

        // BOT-12 (twow-repo#677, AiPlayerbot.BotGroups.OpQueue; the invitee's cohort decides - its accept creates the group): the
        // invite and the accept run on the world thread. Refused (queue full): the pair keeps its cooldown, the next scan
        // decides again.
        Player* const inviter = invite == adhoc_group::Invite::AInvitesB ? bot : other;
        Player* const invitee = invite == adhoc_group::Invite::AInvitesB ? other : bot;
        if (group_ops::QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, invitee->GetGUIDLow()))
        {
            if (EnqueueJoin(inviter, invitee, key) && sPlayerbotAIConfig.botGroupsDiagnostics)
                sLog.outBasic("[BotGroup] kind=adhoc event=invite inviter=%u invitee=%u quest=%u objective=%u joining_group=%u via=queue",
                    inviter->GetGUIDLow(), invitee->GetGUIDLow(), key.questId, uint32(key.objective),
                    inviter->GetGroup() ? inviter->GetGroup()->GetId() : 0u);
            return;  // at most one invite per scan (queued or not)
        }

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

    // BOT-12 (twow-repo#677, AiPlayerbot.BotGroups.OpQueue): an op of this bot (its leave, or a join it is part of) waits on the
    // world thread - nothing to decide until it ran or was dropped. Never set with OpQueue 0.
    if (group_ops::StillPending(ai, bot->GetGUIDLow()))
        return false;

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
