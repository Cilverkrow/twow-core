#include "playerbot/playerbot.h"
#include "playerbot/AdhocGroupWorld.h"
#include "playerbot/BotGroupDiagnostics.h"
#include "playerbot/GroupAiRefresh.h"
#include "playerbot/DeferredLog.h"     // s7r: BotGroups.WorldThreadDefer
#include "playerbot/GroupOpTiming.h"   // s7r: the core share of a join or leave
#include "playerbot/ThreadCpuClock.h"  // s7r: stall control
#include "playerbot/ScanSlicePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/QuestGroupWorld.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "Group/Group.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <vector>

using namespace ai;

namespace ai::adhoc_group
{
ObjectiveKey CurrentObjective(Player* player)
{
    ObjectiveKey key;
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

bool IsCandidateBot(Player* player)
{
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

std::uint32_t ObjectiveCounter(Player* player, ObjectiveKey const& key)
{
    QuestStatusMap& statusMap = player->getQuestStatusMap();
    auto const it = statusMap.find(key.questId);
    if (it == statusMap.end() || key.objective >= QUEST_OBJECTIVES_COUNT)
        return 0;
    return it->second.m_itemcount[key.objective] + it->second.m_creatureOrGOcount[key.objective];
}

bool ObjectiveDone(Player* player, ObjectiveKey const& key)
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

void GroupLevels(Group* group, std::uint32_t& minLevel, std::uint32_t& maxLevel)
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

namespace
{
std::uint32_t UsSince(std::chrono::steady_clock::time_point start)
{
    return std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}

// The bot takes part at all (AdhocGroupAction::isUseful), and its objects are safe to touch.
bool Eligible(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->GetSession())
        return false;
    PlayerbotAI* ai = GetBotAI(bot);
    return ai && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&
        !bot->InBattleGround() && !bot->InBattleGroundQueue();
}

// Bucket of bots that can meet: same map and team, same objective.
std::uint64_t BucketKey(Player* bot, ObjectiveKey const& key)
{
    return (std::uint64_t(bot->GetMapId()) << 48) ^ (std::uint64_t(bot->GetTeam()) << 40) ^
        (std::uint64_t(key.questId) << 8) ^ std::uint64_t(key.objective);
}
}

bool WorldThreadManager::CheckLeave(Player* bot, Group* group, std::uint32_t now)
{
    Registry::Entry entry;
    if (!Groups().Find(group->GetId(), entry))
    {
        tracks.erase(bot->GetGUIDLow());
        return false;  // not an ad-hoc group: the other group rules apply
    }

    Track& track = tracks[bot->GetGUIDLow()];
    if (track.group != group->GetId())
    {
        track = Track();
        track.group = group->GetId();
        track.lastProgress = now;
        track.lastCounter = ObjectiveCounter(bot, entry.key);
    }

    std::uint32_t const counter = ObjectiveCounter(bot, entry.key);
    if (counter != track.lastCounter)
    {
        track.lastCounter = counter;
        track.lastProgress = now;
    }

    Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());
    float const radius = sPlayerbotAIConfig.botGroupsAdhocRadius;
    bool const outOfRange = leader && leader != bot &&
        (leader->GetMapId() != bot->GetMapId() || sServerFacade.GetDistance2d(bot, leader) > 2.0f * radius);
    track.outOfRangeSince = outOfRange ? (track.outOfRangeSince ? track.outOfRangeSince : now) : 0;

    std::uint32_t minLevel = bot->GetLevel();
    std::uint32_t maxLevel = bot->GetLevel();
    GroupLevels(group, minLevel, maxLevel);
    bool const outsideWindow = !LevelWindowOk(minLevel, maxLevel, sPlayerbotAIConfig.botGroupsLevelWindow);
    track.levelWindowSince = outsideWindow ? (track.levelWindowSince ? track.levelWindowSince : now) : 0;

    LeaveFacts facts;
    facts.questInProgress = bot->GetQuestStatus(entry.key.questId) == QUEST_STATUS_INCOMPLETE ||
        bot->GetQuestStatus(entry.key.questId) == QUEST_STATUS_COMPLETE;
    facts.objectiveDone = ObjectiveDone(bot, entry.key);
    facts.inInstance = bot->GetMap()->IsDungeon() || group->IsRaidGroup();
    facts.now = now;
    facts.outOfRangeSince = track.outOfRangeSince;
    facts.levelWindowSince = track.levelWindowSince;
    facts.lastProgress = track.lastProgress;

    // twow-repo#365 HelpUntilDone (owner 25.10.2026): whoever is done stays and follows a member that still works on the
    // objective, until no member does (Leave::GroupDone). The helper's idle clock is the group's freshest progress.
    facts.helpUntilDone = sPlayerbotAIConfig.botGroupsAdhocHelpUntilDone;
    Player* helped = nullptr;
    if (facts.helpUntilDone && facts.objectiveDone && facts.questInProgress)
    {
        float best = 0.0f;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || member == bot || !GetBotAI(member) || member->GetMapId() != bot->GetMapId())
                continue;
            auto const mt = tracks.find(member->GetGUIDLow());
            if (mt != tracks.end() && mt->second.group == group->GetId())
                facts.lastProgress = std::max(facts.lastProgress, mt->second.lastProgress);
            if (ObjectiveDone(member, entry.key))
                continue;
            facts.groupOpen = true;
            float const d = sServerFacade.GetDistance2d(bot, member);
            if (!helped || d < best)
            {
                helped = member;
                best = d;
            }
        }
    }
    if (HelpsGroup(facts) && helped)
    {
        PlayerbotAI* ai = GetBotAI(bot);
        helpingPass.insert(bot->GetGUIDLow());
        auto const known = helpers.find(bot->GetGUIDLow());
        bool const first = known == helpers.end() || known->second != group->GetId();
        if (first || ai->GetMaster() != helped || ai->GetQuestGroupRole() != 1)
        {
            helpers[bot->GetGUIDLow()] = group->GetId();
            // Role 1 = a follower (no LeaveFarAway, the stuck triggers and PosStuck spare it); the set on the bot's own thread,
            // posted on every change: the logout of the followed member resets the helper's strategies (OnPlayerLogout).
            ai->SetQuestGroupRole(1);
            ai->SetMaster(helped);
            ai->RequestGroupAi(group_ai_refresh::Follower);
            if (sPlayerbotAIConfig.botGroupsDiagnostics)
                sLog.outBasic("[BotGroup] kind=adhoc event=help bot=%u group=%u quest=%u objective=%u follows=%u first=%u thread=world",
                    bot->GetGUIDLow(), group->GetId(), entry.key.questId, uint32(entry.key.objective), helped->GetGUIDLow(),
                    first ? 1u : 0u);
        }
    }

    Leave const reason = DecideLeave(facts);
    if (reason == Leave::None)
        return false;

    if (sPlayerbotAIConfig.botGroupsDiagnostics)
        sLog.outBasic("[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u thread=world",
            bot->GetGUIDLow(), group->GetId(), entry.key.questId, uint32(entry.key.objective), LeaveName(reason),
            group->GetMembersCount());

    // The group ends with the second-to-last member (Core disband).
    if (group->GetMembersCount() <= 2)
        Groups().Forget(group->GetId());

    tracks.erase(bot->GetGUIDLow());
    auto const leaveStart = std::chrono::steady_clock::now();
    group_op_timing::lastLeaveCoreUs = 0;
    LeaveGroup(bot, group);
    minute.leaveCoreUsMax = std::max(minute.leaveCoreUsMax, group_op_timing::lastLeaveCoreUs);
    std::uint32_t const leaveUs = UsSince(leaveStart);
    minute.leaveUsMax = std::max(minute.leaveUsMax, leaveUs);
    passLeaveUs += leaveUs;
    ++minute.leaves;
    return true;
}

// LeaveGroupAction::Leave's leave branch without its "free bot stays" rule: the bot
// decided by the ad-hoc leave rules itself. Core path (#301): RemoveMember, Disband.
void WorldThreadManager::LeaveGroup(Player* bot, Group* group)
{
    PlayerbotAI* ai = GetBotAI(bot);
    {
        // s7r (BotGroups.WorldThreadDefer): both lines written later on the bot's own thread.
        deferred_log::Scope const deferLogs(ai, sPlayerbotAIConfig.botGroupsWorldThreadDefer);
        sPlayerbotAIConfig.logEvent(ai, "LeaveGroupAction", group->GetLeaderName(), std::to_string(group->GetMembersCount() - 1));
        bot_group::LogMembership(bot, group, "leave");
    }

    WorldPacket p;
    std::string member = bot->GetName();
    p << uint32(PARTY_OP_LEAVE) << member << uint32(0);
    auto const coreStart = std::chrono::steady_clock::now();
    bot->GetSession()->HandleGroupDisbandOpcode(p);
    group_op_timing::lastLeaveCoreUs = group_op_timing::UsSince(coreStart);   // s7r: the core leave alone

    if (sRandomPlayerbotMgr.IsFreeBot(bot))
        ai->SetMaster(nullptr);
    // twow-repo#365 HelpUntilDone: a helper leaves its follower role behind (the reset below brings its own strategies back).
    if (helpers.erase(bot->GetGUIDLow()))
    {
        ai->SetQuestGroupRole(0);
        ai->SetMaster(nullptr);
    }
    // twow-repo#628 fix: the strategy rebuild and the reset (~10 ms on the world thread in the C short test) on the bot's own thread.
    ai->RequestGroupAi(group_ai_refresh::LeaveReset);
}

bool WorldThreadManager::Join(Player* inviter, Player* invitee, ObjectiveKey const& key, std::uint32_t now)
{
    PlayerbotAI* inviteeAi = GetBotAI(invitee);
    if (!inviteeAi)
        return false;

    // The existing invite path; the core refuses a full group or a pending invite.
    WorldPacket p;
    uint32 const rolesMask = 0;
    p << invitee->GetName();
    p << rolesMask;
    inviter->GetSession()->HandleGroupInviteOpcode(p);
    if (!invitee->GetGroupInvite())
        return false;

    // The invitee's own accept path (security check, master, strategies), run here
    // instead of on its map thread. Its queued invite event later finds no invite.
    group_op_timing::lastAcceptCoreUs = 0;
    inviteeAi->DoSpecificAction("accept invitation", Event("adhoc group", "", inviter), true);
    minute.joinCoreUsMax = std::max(minute.joinCoreUsMax, group_op_timing::lastAcceptCoreUs);   // s7r: the core join alone

    Group* group = invitee->GetGroup();
    if (!group || !group->IsMember(inviter->GetObjectGuid()))
    {
        if (invitee->GetGroupInvite())
            invitee->UninviteFromGroup();
        return false;
    }

    Registry::Entry entry;
    if (!Groups().Find(group->GetId(), entry) && AllRosterBots(group))
    {
        Groups().Register(group->GetId(), key, now);
        // Design section 4: free-for-all, so there is no roll delay on quest items.
        group->SetLootMethod(FREE_FOR_ALL);
        group->SendUpdate();

        if (sPlayerbotAIConfig.botGroupsDiagnostics)
            sLog.outBasic("[BotGroup] kind=adhoc event=register bot=%u group=%u quest=%u objective=%u members=%u thread=world",
                invitee->GetGUIDLow(), group->GetId(), key.questId, uint32(key.objective), group->GetMembersCount());
    }
    return true;
}

void WorldThreadManager::FlushMinute(std::uint32_t now)
{
    if (!minute.start)
        minute.start = now;
    if (now - minute.start < 60)
        return;

    if (sPlayerbotAIConfig.botGroupsDiagnostics && minute.passes)
        sLog.outBasic("[BotGroup] kind=adhoc event=minute passes=%u candidates=%u invites=%u joins=%u leaves=%u tracked=%u pass_us_max=%u join_us_max=%u leave_us_max=%u scan_us_max=%u bots=%u slices=%u join_core_us_max=%u leave_core_us_max=%u stall_us_max=%u scan_budget_us=%u defer=%u",
            minute.passes, minute.candidates, minute.invites, minute.joins, minute.leaves, uint32(tracks.size()), minute.passUsMax,
            minute.joinUsMax, minute.leaveUsMax, minute.scanUsMax, minute.bots, minute.slices,
            minute.joinCoreUsMax, minute.leaveCoreUsMax, minute.stallUsMax, sPlayerbotAIConfig.botGroupsAdhocScanBudgetUs,
            sPlayerbotAIConfig.botGroupsWorldThreadDefer ? 1u : 0u);
    minute = Minute();
    minute.start = now;
}

// twow-repo#365 HelpUntilDone (OB-10 review #660 B1): every helper not seen helping in this pass drops its follower role -
// its group ended or is no ad-hoc group any more, it is no candidate any more, it is in another group or helps no longer.
// Only the manager's own helpers, never by role (S7 followers carry role 1 too); a bot S7 owns now keeps its S7 role.
void WorldThreadManager::SweepHelpers(std::map<std::uint32_t, Player*> const& bots)
{
    for (auto it = helpers.begin(); it != helpers.end();)
    {
        auto const found = bots.find(it->first);
        Player* bot = found == bots.end() ? nullptr : found->second;
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        Group* group = bot ? bot->GetGroup() : nullptr;

        HelperFacts facts;
        facts.loggedOut = !ai || !bot->GetSession();
        facts.s7Owns = quest_groups::OwnsBot(it->first);
        facts.transient = bot && (!bot->IsInWorld() || bot->IsBeingTeleported());
        facts.helping = helpingPass.count(it->first) != 0;
        facts.otherGroup = group && group->GetId() != it->second;
        facts.realMaster = !facts.loggedOut && !facts.transient && ai->HasRealPlayerMaster();
        HelperExit const exit = DecideHelperExit(facts);
        if (exit == HelperExit::Keep)
        {
            ++it;
            continue;
        }

        if (exit == HelperExit::RoleOnly || exit == HelperExit::Release)
            ai->SetQuestGroupRole(0);
        if (exit == HelperExit::Release)
        {
            // Its master as after an accept (the group leader), none without a group; the joined or the left strategies back.
            Player* leader = group ? sObjectMgr.GetPlayer(group->GetLeaderGuid()) : nullptr;
            ai->SetMaster(leader && leader != bot && GetBotAI(leader) ? leader : nullptr);
            ai->RequestGroupAi(group ? group_ai_refresh::JoinReset : group_ai_refresh::LeaveReset);
        }
        if (sPlayerbotAIConfig.botGroupsDiagnostics)
            sLog.outBasic("[BotGroup] kind=adhoc event=help_end bot=%u group=%u exit=%s in_group=%u thread=world", it->first,
                it->second, HelperExitName(exit), group ? group->GetId() : 0u);
        it = helpers.erase(it);
    }
}

void WorldThreadManager::ReleaseHelpers(std::map<std::uint32_t, Player*> const& bots)
{
    helpingPass.clear();
    SweepHelpers(bots);
}

void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)
{
    if (!pass.active)
    {
        if (now - lastPass < sPlayerbotAIConfig.botGroupsAdhocScanIntervalSeconds)
            return;
        lastPass = now;
        FlushMinute(now);  // a finished minute is logged before this pass counts

        // Bots that logged out since the last pass take their leave tracking with them,
        // and registry entries of groups disbanded elsewhere go (bounded registry).
        for (auto it = tracks.begin(); it != tracks.end();)
            it = bots.count(it->first) ? std::next(it) : tracks.erase(it);
        Groups().ForgetGone([](std::uint32_t groupId) { return sObjectMgr.GetGroupById(groupId) != nullptr; });

        pass = Pass();
        pass.active = true;
        pass.guids.reserve(bots.size());
        for (auto const& [guid, bot] : bots)
            pass.guids.push_back(guid);
        minute.bots = std::max(minute.bots, std::uint32_t(bots.size()));
    }
    auto const passStart = std::chrono::steady_clock::now();
    std::uint64_t const cpuStart = thread_cpu::NowUs();   // s7r: stall control (ThreadCpuClock.h)
    passLeaveUs = 0;

    // twow-repo#628 (ScanSlices): this update's share of the bots; 0 or 1 = all of them at once.
    ScanSlice(bots, scan_slice::SliceEnd(pass.guids.size(), pass.next, sPlayerbotAIConfig.botGroupsAdhocScanSlices), now);
    ++pass.updates;

    // The scan alone: leave rules and candidates of this update's bots, without the leaves themselves.
    std::uint32_t const scanUs = UsSince(passStart);
    minute.scanUsMax = std::max(minute.scanUsMax, scanUs > passLeaveUs ? scanUs - passLeaveUs : 0u);

    if (pass.next >= pass.guids.size())
    {
        // #365 HelpUntilDone: the leave rules of every slice ran, so every helper still helping was seen. Before the pairs:
        // a released helper joins another group with its own strategies back.
        SweepHelpers(bots);
        helpingPass.clear();
        Pairs(bots, now);
        ++minute.passes;
        minute.slices = pass.updates;
        pass = Pass();
    }
    std::uint32_t const wallUs = UsSince(passStart);
    minute.passUsMax = std::max(minute.passUsMax, wallUs);
    minute.stallUsMax = std::max(minute.stallUsMax, thread_cpu::StallUs(wallUs, cpuStart, thread_cpu::NowUs()));
}

// Stages 1 and 2 for the bots pass.guids[pass.next, end): first the leave rules of all of them, then their candidates - with
// one slice exactly the order of the whole pass.
void WorldThreadManager::ScanSlice(std::map<std::uint32_t, Player*> const& bots, std::size_t end, std::uint32_t now)
{
    // 1. Leave rules for every bot in an ad-hoc group.
    std::vector<Player*> eligible;
    eligible.reserve(end > pass.next ? end - pass.next : 0);
    // s7r (BotGroups.AdHoc.ScanBudgetUs): the slice also ends once it took the budget; the rest goes to the next update.
    std::uint32_t const budgetUs = sPlayerbotAIConfig.botGroupsAdhocScanBudgetUs;
    auto const sliceStart = std::chrono::steady_clock::now();
    std::size_t const first = pass.next;
    for (; pass.next < end; ++pass.next)
    {
        if (scan_slice::BudgetSpent(budgetUs, budgetUs ? UsSince(sliceStart) : 0u, pass.next - first))
            break;
        std::uint32_t const guid = pass.guids[pass.next];
        auto const it = bots.find(guid);
        Player* bot = it == bots.end() ? nullptr : it->second;
        if (!Eligible(bot))
        {
            tracks.erase(guid);
            continue;
        }
        if (Group* group = bot->GetGroup())
        {
            if (CheckLeave(bot, group, now))
                continue;
        }
        else
            tracks.erase(guid);
        eligible.push_back(bot);
    }

    // 2. Candidates by objective: ungrouped bots and ad-hoc leaders with room. No
    // value per neighbour, no world scan (#351): one key per bot and pass.
    for (Player* bot : eligible)
    {
        Group* group = bot->GetGroup();
        Registry::Entry entry;
        bool const inAdhoc = group && Groups().Find(group->GetId(), entry);
        if (group && !inAdhoc)
            continue;  // one ad-hoc group per bot; any other group is never a candidate
        if (inAdhoc && (!group->IsLeader(bot->GetObjectGuid()) || group->GetMembersCount() >= sPlayerbotAIConfig.botGroupsMaxBots))
            continue;  // the leader invites for its group

        ObjectiveKey const key = inAdhoc ? entry.key : CurrentObjective(bot);
        if (!key.IsValid() || !IsCandidateBot(bot))
            continue;
        pass.buckets[BucketKey(bot, key)].push_back({ bot->GetGUIDLow(), inAdhoc ? group->GetId() : 0u, key });
        ++minute.candidates;
    }
}

// 3. Pairs inside a bucket: radius, pair cooldown, level window, who invites. Each candidate is resolved again first; one whose
// bot or group changed since its slice (gone, grouped, ungrouped, no longer leader, group full) is left out. With one slice
// nothing changes in between, so the pairs are those of the whole pass.
void WorldThreadManager::Pairs(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)
{
    struct Live
    {
        Player* bot;
        Group* group;  // its ad-hoc group (led by it) or null
        ObjectiveKey key;
    };
    std::uint32_t const cooldown = sPlayerbotAIConfig.botGroupsAdhocPairCooldownSeconds;
    float const radius = sPlayerbotAIConfig.botGroupsAdhocRadius;
    std::unordered_set<std::uint32_t> done;  // one invite per bot and pass
    for (auto& [bucket, list] : pass.buckets)
    {
        if (list.size() < 2)
            continue;
        std::vector<Live> candidates;
        candidates.reserve(list.size());
        for (Candidate const& c : list)
        {
            auto const it = bots.find(c.guid);
            Player* bot = it == bots.end() ? nullptr : it->second;
            if (!Eligible(bot))
                continue;
            Group* group = bot->GetGroup();
            if (c.groupId ? (!group || group->GetId() != c.groupId || !group->IsLeader(bot->GetObjectGuid()) ||
                    group->GetMembersCount() >= sPlayerbotAIConfig.botGroupsMaxBots) : group != nullptr)
                continue;
            candidates.push_back({ bot, c.groupId ? group : nullptr, c.key });
        }
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            Live const& a = candidates[i];
            if (done.count(a.bot->GetGUIDLow()))
                continue;
            for (std::size_t j = i + 1; j < candidates.size(); ++j)
            {
                Live const& b = candidates[j];
                if (done.count(b.bot->GetGUIDLow()) || a.key != b.key)
                    continue;  // a bucket collision of two objectives
                if (a.group && b.group)
                    continue;  // two ad-hoc groups are not merged
                if (sServerFacade.GetDistance2d(a.bot, b.bot) > radius)
                    continue;
                if (PairCooldowns().IsBlocked(a.bot->GetGUIDLow(), b.bot->GetGUIDLow(), now))
                    continue;

                std::uint32_t minLevel = std::min(a.bot->GetLevel(), b.bot->GetLevel());
                std::uint32_t maxLevel = std::max(a.bot->GetLevel(), b.bot->GetLevel());
                if (a.group)
                    GroupLevels(a.group, minLevel, maxLevel);
                if (b.group)
                    GroupLevels(b.group, minLevel, maxLevel);
                if (!LevelWindowOk(minLevel, maxLevel, sPlayerbotAIConfig.botGroupsLevelWindow))
                    continue;

                Invite const invite = Decide(a.bot->GetGUIDLow(), a.group != nullptr, a.group != nullptr,
                    b.bot->GetGUIDLow(), b.group != nullptr, b.group != nullptr);
                if (invite == Invite::None)
                    continue;
                Player* inviter = invite == Invite::AInvitesB ? a.bot : b.bot;
                Player* invitee = invite == Invite::AInvitesB ? b.bot : a.bot;
                Group* joining = a.group ? a.group : b.group;

                // An invite, joined or not, puts the pair on the cooldown (design 3.3).
                PairCooldowns().Block(a.bot->GetGUIDLow(), b.bot->GetGUIDLow(), now + cooldown, now);
                done.insert(a.bot->GetGUIDLow());
                done.insert(b.bot->GetGUIDLow());
                ++minute.invites;

                if (sPlayerbotAIConfig.botGroupsDiagnostics)
                    sLog.outBasic("[BotGroup] kind=adhoc event=invite inviter=%u invitee=%u quest=%u objective=%u joining_group=%u thread=world",
                        inviter->GetGUIDLow(), invitee->GetGUIDLow(), a.key.questId, uint32(a.key.objective),
                        joining ? joining->GetId() : 0u);
                auto const joinStart = std::chrono::steady_clock::now();
                if (Join(inviter, invitee, a.key, now))
                    ++minute.joins;
                minute.joinUsMax = std::max(minute.joinUsMax, UsSince(joinStart));
                break;  // at most one invite per bot and pass
            }
        }
    }
}

WorldThreadManager& WorldManager()
{
    static WorldThreadManager manager;
    return manager;
}
}
