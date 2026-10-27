#include "playerbot/playerbot.h"
#include "playerbot/QuestGroupWorld.h"
#include "playerbot/BotGroupDiagnostics.h"
#include "playerbot/GroupAiRefresh.h"
#include "playerbot/DeferredLog.h"     // s7r: BotGroups.WorldThreadDefer
#include "playerbot/GroupOpTiming.h"   // s7r: the core share of a join or leave
#include "playerbot/ThreadCpuClock.h"  // s7r: stall control
#include "playerbot/ScanSlicePolicy.h"
#include "playerbot/GuidPosition.h"
#include "playerbot/BotSlots.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "Group/Group.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <ctime>
#include <string>

using namespace ai;

namespace ai::quest_groups
{
namespace
{
constexpr float GatherRadius = 30.0f;
constexpr std::uint32_t MaxForces = 20;   // re-forcing the leader's target (moves failed, condition flipped)

std::uint32_t UsSince(std::chrono::steady_clock::time_point start)
{
    return std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}

PlayerbotAI* Ai(Player* bot)
{
    return bot ? GetBotAI(bot) : nullptr;
}

TravelTarget* Target(PlayerbotAI* ai)
{
    return ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
}

// A roster bot the manager may touch at all: in the world, no real player in charge, no battleground or instance,
// alive, not on a taxi or a teleport.
bool Eligible(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->GetSession() || !bot->IsAlive() || bot->IsTaxiFlying())
        return false;
    PlayerbotAI* ai = Ai(bot);
    return ai && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&
        !bot->InBattleGround() && !bot->InBattleGroundQueue() && !bot->GetMap()->IsDungeon();
}

bool ObjectiveOpen(QuestStatusData const& st, Quest const* q, std::uint8_t i)
{
    return st.m_itemcount[i] < q->ReqItemCount[i] || st.m_creatureOrGOcount[i] < q->ReqCreatureOrGOCount[i];
}

// Open objectives of the bot's quest (0 = done or not in progress); dropOnly = some open objective is an item without a kill.
std::uint32_t OpenObjectives(Player* bot, std::uint32_t questId, bool* dropOnly = nullptr)
{
    Quest const* q = sObjectMgr.GetQuestTemplate(questId);
    QuestStatusData const* st = bot->GetQuestStatusData(questId);
    if (!q || !st || st->m_rewarded || st->m_status != QUEST_STATUS_INCOMPLETE)
        return 0;
    std::uint32_t open = 0;
    for (std::uint8_t i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (ObjectiveOpen(*st, q, i))
        {
            ++open;
            if (dropOnly && q->ReqItemCount[i] && !q->ReqCreatureOrGOCount[i])
                *dropOnly = true;
        }
    return open;
}

std::uint32_t Counter(Player* bot, std::uint32_t questId)
{
    QuestStatusData const* st = bot->GetQuestStatusData(questId);
    if (!st)
        return 0;
    std::uint32_t sum = 0;
    for (std::uint8_t i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        sum += st->m_itemcount[i] + st->m_creatureOrGOcount[i];
    return sum + (st->m_status == QUEST_STATUS_COMPLETE ? 1000u : 0u) + (st->m_rewarded ? 1000u : 0u);
}

bool Complete(Player* bot, std::uint32_t questId)
{
    QuestStatusData const* st = bot->GetQuestStatusData(questId);
    return st && (st->m_rewarded || st->m_status == QUEST_STATUS_COMPLETE);
}

bool TurnedIn(Player* bot, std::uint32_t questId)
{
    QuestStatusData const* st = bot->GetQuestStatusData(questId);
    return st && st->m_rewarded;
}

}   // namespace - StagedOps: FindQuestDest/ForceQuestTravel have external linkage (declared in QuestGroupWorld.h, run on the bot thread)

// The nearest destination of the quest for one objective (objIdx 0..3) or its taker (-1). GetDestinations, not
// GetPartitions (that one waits for a worker thread). onlyPossible = false: an elite objective is the whole point.
TravelDestination* FindQuestDest(Player* bot, std::uint32_t questId, int objIdx, WorldPosition*& point)
{
    point = nullptr;
    PlayerTravelInfo info(bot);
    std::uint32_t const flag = objIdx < 0 ? uint32(TravelDestinationPurpose::QuestTaker)
                                          : (uint32(TravelDestinationPurpose::QuestObjective1) << objIdx);
    WorldPosition const botPos(bot);
    TravelDestination* best = nullptr;
    float bestDist = FLT_MAX;
    for (TravelDestination* d : sTravelMgr.GetDestinations(info, flag, { int32(questId) }, false, 0.0f))
    {
        QuestTravelDestination const* qd = dynamic_cast<QuestTravelDestination const*>(d);
        if (!qd || qd->GetQuestId() != questId)
            continue;
        float const dist = d->DistanceTo(botPos);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = d;
        }
    }
    if (best)
        point = best->GetClosestPoint(botPos);
    return point ? best : nullptr;
}

void ResetTravelValues(PlayerbotAI* ai)
{
    AiObjectContext* ctx = ai->GetAiObjectContext();
    ctx->GetValue<GuidPosition>("rpg target")->Reset();
    ctx->GetValue<ObjectGuid>("attack target")->Reset();
    ctx->GetValue<bool>("travel target active")->Reset();
}

// A forced target with a condition (a forced target without one never ends): the objective while anyone following still
// needs it, the taker while the bot can turn in (ChooseTravelTargetAction's own condition strings).
bool ForceQuestTravel(Player* bot, std::uint32_t questId, int objIdx)
{
    PlayerbotAI* ai = Ai(bot);
    TravelTarget* t = Target(ai);
    if (t->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;   // an async search is running: next pass
    if (objIdx < 0 && t->IsTurnInParked(questId))
        return false;
    WorldPosition* point = nullptr;
    TravelDestination* dest = FindQuestDest(bot, questId, objIdx, point);
    if (!dest)
        return false;
    t->SetTarget(dest, point);
    t->SetForced(true);
    t->SetRelevance(196);   // below guild order 198 and guild founding 197
    std::string const cond = objIdx >= 0
        ? "group or::{following party,need quest objective::{" + std::to_string(questId) + "," + std::to_string(objIdx) + "}}"
        : "can turn in quest npc::" + std::to_string(dest->GetEntry());
    t->SetConditions({ cond });
    t->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
    t->SetExpireIn(HOUR * IN_MILLISECONDS);
    ResetTravelValues(ai);
    return true;
}

namespace
{

// The leader's forced target is on this quest (objective or taker).
bool LeaderOnQuest(Player* leader, std::uint32_t questId)
{
    TravelTarget* t = Target(Ai(leader));
    QuestTravelDestination const* qd = t->IsForced() ? dynamic_cast<QuestTravelDestination const*>(t->GetDestination()) : nullptr;
    return qd && qd->GetQuestId() == questId;
}

// First objective index some member still has open (the leader's own first), -1 = none.
int OpenObjectiveIndex(std::vector<Player*> const& members, std::uint32_t questId)
{
    Quest const* q = sObjectMgr.GetQuestTemplate(questId);
    if (!q)
        return -1;
    for (Player* p : members)
    {
        QuestStatusData const* st = p->GetQuestStatusData(questId);
        if (!st || st->m_rewarded || st->m_status != QUEST_STATUS_INCOMPLETE)
            continue;
        for (std::uint8_t i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            if (ObjectiveOpen(*st, q, i))
                return i;
    }
    return -1;
}

SavedTravel SaveTravel(TravelTarget* t)
{
    SavedTravel s;
    TravelStatus const st = t->GetStatus();
    TravelDestination* d = t->GetDestination();
    if (d && !dynamic_cast<TemporaryTravelDestination*>(d) && !t->IsGroupCopy() && !t->GetGapWindowEnd() && !t->GetBandTripEnd() &&
        !d->IsBandHub() &&
        (st == TravelStatus::TRAVEL_STATUS_READY || st == TravelStatus::TRAVEL_STATUS_TRAVEL || st == TravelStatus::TRAVEL_STATUS_WORK))
    {
        s.dest = d;
        s.pos = t->GetPosition();
        s.conditions = t->GetConditions();
        s.forced = t->IsForced();
        s.relevance = t->GetRelevance();
        s.valid = true;
    }
    return s;
}

// Back to the bot's own travel: the saved target if it was a real one, else none (a fresh choice on the next tick).
// A stale target (inactive since) is ended by TravelTarget::CheckStatus itself.
void RestoreTravel(PlayerbotAI* ai, SavedTravel const& s)
{
    TravelTarget* t = Target(ai);
    if (!s.valid)
    {
        sTravelMgr.SetNullTravelTarget(t);
        t->SetForced(false);
        ResetTravelValues(ai);
        return;
    }
    t->SetTarget(s.dest, s.pos);
    t->SetConditions(s.conditions);
    t->SetForced(s.forced);
    t->SetRelevance(s.relevance);
    t->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
    if (s.forced)
        t->SetExpireIn(HOUR * IN_MILLISECONDS);
    ResetTravelValues(ai);
}

// A plain follower of the leader while the group works: no own travel, rpg, grind or group leaving.
// AcceptInvitation's ResetStrategies runs before this; ResetStrategies elsewhere can undo it, so every pass re-checks.
// twow-repo#628 fix: the sets are posted (group_ai_refresh::Follower / TurnIn) and run on the bot's own thread (ApplyRoleSet).
std::string const FollowerSet = "-wander,+follow,-travel,-tfish,-rpg,-grind,-attack tagged,-group,-adhoc group,-lfg,-bg,"
                                "-guild,-start duel,-duel,-gather";
// Turn-in phase: every member goes to the taker on its own forced target (a follower does not turn in on its own).
std::string const TurnInSet = "-follow,+travel,-group,-adhoc group";

void MakeFollower(PlayerbotAI* ai, Player* leader)
{
    if (ai->GetMaster() != leader)
        ai->SetMaster(leader);
    ai->RequestGroupAi(group_ai_refresh::Follower);
}

bool FollowerIntact(PlayerbotAI* ai, Player* leader)
{
    return ai->GetMaster() == leader && ((ai->PendingGroupAiRequests() & group_ai_refresh::Follower) ||
        (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) &&
        !ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT) && !ai->HasStrategy("group", BotState::BOT_STATE_NON_COMBAT)));
}

// Turn-in phase: the turn-in set (TurnInSet), posted to the bot.
void MakeTurnIn(PlayerbotAI* ai)
{
    ai->RequestGroupAi(group_ai_refresh::TurnIn);
}

// The invite path of the core with the leader as inviter (the inviter becomes leader and master of the invitee), then
// the invitee's own accept action - both on the world thread.
// StagedOps sub-times: the core's invite opcode and the accept action, each in us (null: not measured).
bool InviteAccept(Player* leader, Player* member, std::uint32_t* inviteUs = nullptr, std::uint32_t* acceptUs = nullptr)
{
    PlayerbotAI* memberAi = Ai(member);
    if (!memberAi)
        return false;
    WorldPacket p;
    uint32 const rolesMask = 0;
    p << member->GetName();
    p << rolesMask;
    auto const t0 = std::chrono::steady_clock::now();
    leader->GetSession()->HandleGroupInviteOpcode(p);
    if (inviteUs)
        *inviteUs = UsSince(t0);
    if (!member->GetGroupInvite())
        return false;
    auto const t1 = std::chrono::steady_clock::now();
    memberAi->DoSpecificAction("accept invitation", Event("quest group", "", leader), true);
    if (acceptUs)
        *acceptUs = UsSince(t1);
    Group* group = member->GetGroup();
    if (!group || !group->IsMember(leader->GetObjectGuid()))
    {
        if (member->GetGroupInvite())
            member->UninviteFromGroup();
        return false;
    }
    return true;
}

// The core leave path (#301: RemoveMember, Disband with the second-to-last member).
void LeaveCore(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;
    PlayerbotAI* ai = Ai(bot);
    {
        // s7r (BotGroups.WorldThreadDefer): both lines written later on the bot's own thread.
        deferred_log::Scope const deferLogs(ai, sPlayerbotAIConfig.botGroupsWorldThreadDefer);
        sPlayerbotAIConfig.logEvent(ai, "LeaveGroupAction", group->GetLeaderName(), std::to_string(group->GetMembersCount() - 1));
        bot_group::LogMembership(bot, group, "leave");
    }
    WorldPacket p;
    std::string name = bot->GetName();
    p << uint32(PARTY_OP_LEAVE) << name << uint32(0);
    auto const coreStart = std::chrono::steady_clock::now();
    bot->GetSession()->HandleGroupDisbandOpcode(p);
    group_op_timing::lastLeaveCoreUs = group_op_timing::UsSince(coreStart);   // s7r: the core leave alone
}

Player* Find(std::map<std::uint32_t, Player*> const& bots, std::uint32_t guid)
{
    auto const it = bots.find(guid);
    return it == bots.end() ? nullptr : it->second;
}
}

// --- queries -------------------------------------------------------------------------------------------------------------------
bool IsMember(PlayerbotAI* ai)
{
    return ai && ai->GetQuestGroupRole() != 0;
}

bool IsFollower(PlayerbotAI* ai)
{
    return ai && ai->GetQuestGroupRole() == 1;
}

void NoteLeaderStuck(PlayerbotAI* ai)
{
    if (ai && ai->GetQuestGroupRole() == 2)
        ai->SetQuestGroupLeaderStuck(true);
}

QueueStore& Queue()
{
    static QueueStore store;
    return store;
}

bool OwnsBot(std::uint32_t guid)
{
    return WorldManager().Owns(guid);
}

std::uint32_t QueuedAge(std::uint32_t guid, std::uint32_t questId)
{
    return Queue().QueuedAge(guid, questId, std::uint32_t(time(nullptr)));
}

std::uint32_t OldestQueuedAge(std::uint32_t guid)
{
    return Queue().OldestQueuedAge(guid, std::uint32_t(time(nullptr)));
}

// twow-repo#628 fix: the role set on the bot's own thread (PlayerbotAI::ApplyGroupAiRequests, after a reset of the same requests);
// a bot released meanwhile keeps its own strategies.
void ApplyRoleSet(PlayerbotAI* ai, bool turnIn)
{
    if (!ai || ai->GetQuestGroupRole() == 0)
        return;
    ai->ChangeStrategy(turnIn ? TurnInSet : FollowerSet, BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-group", BotState::BOT_STATE_DEAD);
}

// --- manager ---------------------------------------------------------------------------------------------------------------------
void WorldThreadManager::Release(Player* bot, Member const& m, bool leaveGroup)
{
    PlayerbotAI* ai = Ai(bot);
    if (!ai)
        return;
    if (leaveGroup)
        LeaveCore(bot);
    ai->SetQuestGroupRole(0);
    ai->SetQuestGroupLeaderStuck(false);
    // LeaveGroupAction::Leave does not reset a free bot with a bot master; the S7 strategy set must go in any case.
    ai->SetMaster(nullptr);
    // twow-repo#628 fix: the strategy rebuild and the reset on the bot's own thread (it also drops a pending S7 set).
    ai->RequestGroupAi(group_ai_refresh::LeaveReset);
    RestoreTravel(ai, m.travel);
}

void WorldThreadManager::DissolveGroup(std::map<std::uint32_t, Player*> const& bots, HelpGroup& g, Dissolve reason, std::uint32_t now)
{
    std::uint32_t turnedIn = 0;
    for (Member const& m : g.members)
    {
        memberOf.erase(m.guid);
        Player* bot = Find(bots, m.guid);
        if (bot && TurnedIn(bot, g.questId))
            ++turnedIn;
        if (reason == Dissolve::TurnedIn || (bot && TurnedIn(bot, g.questId)))
            Queue().Forget(m.guid, g.questId);
        else
            Queue().Failed(m.guid, g.questId, now, sPlayerbotAIConfig.questGroupsRetryCooldownMinutes * 60);
        if (!bot)
            continue;
        if (Staged())
        {
            StageRelease(g.groupId, m);   // StagedOps: one member per update (Release decides leave/stay then)
            continue;
        }
        Group* group = bot->GetGroup();
        Release(bot, m, group && group->GetId() == g.groupId);
    }
    sLog.outBasic("[QuestGroup] state=dissolved group=%u quest=%u size=%u reason=%s minutes=%u phase=%s turned_in=%u cohort=%c mixed=%u",
        g.groupId, g.questId, uint32(g.members.size()), DissolveName(reason), (now - g.formedAt) / 60, PhaseName(g.phase), turnedIn,
        g.cohort, g.mixed ? 1u : 0u);
    ++minute.dissolved;
}

void WorldThreadManager::Tend(std::map<std::uint32_t, Player*> const& bots, HelpGroup& g, std::uint32_t now, Dissolve& out)
{
    out = Dissolve::None;
    Player* leader = Find(bots, g.leader);
    Group* group = leader ? leader->GetGroup() : nullptr;

    // Members still here and still in the group; who left on its own is released now.
    std::vector<Player*> present;
    bool realPlayer = false;
    bool abandoned = false;
    for (auto it = g.members.begin(); it != g.members.end();)
    {
        Player* bot = Find(bots, it->guid);
        PlayerbotAI* ai = Ai(bot);
        bool const inGroup = bot && bot->GetGroup() && bot->GetGroup()->GetId() == g.groupId;
        if (!bot || !ai || !inGroup)
        {
            memberOf.erase(it->guid);
            if (bot && ai)
                Release(bot, *it, false);
            it = g.members.erase(it);
            continue;
        }
        if (ai->HasRealPlayerMaster())
            realPlayer = true;
        QuestStatusData const* st = bot->GetQuestStatusData(g.questId);
        if (!st || (st->m_status == QUEST_STATUS_NONE && !st->m_rewarded))
        {
            if (it->guid == g.leader)
                abandoned = true;   // the leader dropped the quest
            else
            {
                memberOf.erase(it->guid);
                if (Staged())
                    StageRelease(g.groupId, *it);   // StagedOps: the leave opcode in its own update
                else
                    Release(bot, *it, true);
                it = g.members.erase(it);
                continue;
            }
        }
        present.push_back(bot);
        ++it;
    }

    // The core picks a new leader when the leader leaves; adopt it if it is one of ours (re-election).
    bool leaderLost = !leader || !group || group->GetId() != g.groupId;
    if (!leaderLost && group->GetLeaderGuid() != leader->GetObjectGuid())
    {
        std::uint32_t const next = group->GetLeaderGuid().GetCounter();
        auto const it = std::find_if(g.members.begin(), g.members.end(), [&](Member const& m) { return m.guid == next; });
        if (it != g.members.end())
        {
            sLog.outBasic("[QuestGroup] state=leader group=%u quest=%u old=%u new=%u", g.groupId, g.questId, g.leader, next);
            g.leader = next;
            leader = Find(bots, next);
        }
        else
            leaderLost = true;
    }

    GroupFacts f;
    f.phase = g.phase;
    f.now = now;
    f.formedAt = g.formedAt;
    f.phaseSince = g.phaseSince;
    f.members = std::uint32_t(present.size());
    f.realPlayer = realPlayer;
    f.leaderLost = leaderLost || !leader;
    // The leader is stuck when it gets no ApproachYards closer to its forced target for LeaderStallSeconds (PosStuck and S3c
    // skip forced targets), when a hook flagged it (NoteLeaderStuck), or when it left the map.
    bool approachStuck = false;
    PlayerbotAI* leaderAi = Ai(leader);
    if (leaderAi && g.phase != Phase::TurnIn)
    {
        TravelTarget* lt = Target(leaderAi);
        TravelDestination* d = lt->IsForced() ? lt->GetDestination() : nullptr;
        WorldPosition const leaderPos(leader);
        bool const travelling = d && lt->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL && d->HasPointsOnMap(leaderPos);
        // The fight counts into the window, but a group never dissolves in the middle of an (elite) fight (OB-10, as PosStuck).
        approachStuck = g.leaderWatch.Observe(std::uintptr_t(d), travelling ? d->DistanceTo(leaderPos) : 0.0f, travelling, now) &&
            !leader->IsInCombat();
    }
    f.leaderStuck = leaderAi && (approachStuck || leaderAi->TakeQuestGroupLeaderStuck() || leader->GetMapId() != g.mapId);
    f.abandoned = abandoned;
    f.allComplete = !present.empty();
    f.allTurnedIn = !present.empty();
    f.gathered = !present.empty();
    for (Member& m : g.members)
    {
        Player* bot = Find(bots, m.guid);
        if (!bot)
            continue;
        std::uint32_t const c = Counter(bot, g.questId);
        if (c != m.lastCounter)
        {
            m.lastCounter = c;
            g.lastProgress = now;
        }
        if (!Complete(bot, g.questId))
            f.allComplete = false;
        if (!TurnedIn(bot, g.questId))
            f.allTurnedIn = false;
        if (leader && bot != leader &&
            (bot->GetMapId() != leader->GetMapId() || sServerFacade.GetDistance2d(bot, leader) > GatherRadius))
            f.gathered = false;
    }
    f.lastProgress = g.lastProgress;

    Timers t;
    t.rendezvousMinutes = sPlayerbotAIConfig.questGroupsRendezvousMinutes;
    t.noProgressMinutes = sPlayerbotAIConfig.questGroupsNoProgressMinutes;
    t.turnInWaitMinutes = sPlayerbotAIConfig.questGroupsTurnInWaitMinutes;
    t.maxQuestMinutes = sPlayerbotAIConfig.questGroupsMaxQuestMinutes;
    Step const step = Decide(f, t);
    if (step.dissolve != Dissolve::None)
    {
        out = step.dissolve;
        return;
    }

    if (step.next != g.phase)
    {
        g.phase = step.next;
        g.phaseSince = now;
        sLog.outBasic("[QuestGroup] state=%s group=%u quest=%u size=%u minutes=%u cohort=%c", PhaseName(g.phase), g.groupId,
            g.questId, uint32(present.size()), (now - g.formedAt) / 60, g.cohort);
        if (g.phase == Phase::TurnIn)
            for (Player* bot : present)
            {
                MakeTurnIn(Ai(bot));
                bool forced = false;
                Force(bot, g.questId, -1, forced);
            }
    }

    // Keep the roles: followers follow the (possibly new) leader, the leader is forced onto the quest.
    if (g.phase != Phase::TurnIn)
    {
        for (Player* bot : present)
        {
            PlayerbotAI* ai = Ai(bot);
            if (bot == leader)
            {
                ai->SetQuestGroupRole(2);
                continue;
            }
            ai->SetQuestGroupRole(1);
            if (!FollowerIntact(ai, leader))
                MakeFollower(ai, leader);
        }
        if (!LeaderOnQuest(leader, g.questId) && g.forces < MaxForces)
        {
            int const idx = OpenObjectiveIndex(present, g.questId);
            bool forced = false;
            if (idx >= 0)
                Force(leader, g.questId, idx, forced);
            if (forced)
                ++g.forces;
        }
    }
    else
    {
        // Whoever has turned in waits; whoever has not keeps (or gets back) its forced target to the taker.
        for (Player* bot : present)
            if (!TurnedIn(bot, g.questId) && !LeaderOnQuest(bot, g.questId))
            {
                bool forced = false;
                Force(bot, g.questId, -1, forced);
            }
    }
}

// --- StagedOps (AiPlayerbot.QuestGroups.StagedOps) -----------------------------------------------------------------------
bool WorldThreadManager::Staged() const
{
    return sPlayerbotAIConfig.questGroupsStagedOps;
}

// The forced quest travel: directly (StagedOps 0, as before) or posted to the bot's own thread. A post still pending is not
// posted again (Tend runs every update, the bot takes it in its next UpdateAI).
void WorldThreadManager::Force(Player* bot, std::uint32_t questId, int objIdx, bool& forced)
{
    forced = false;
    PlayerbotAI* ai = Ai(bot);
    if (!ai)
        return;
    if (!Staged())
    {
        forced = ForceQuestTravel(bot, questId, objIdx);
        return;
    }
    if (ai->PendingGroupAiRequests() & group_ai_refresh::ForceTravel)
        return;
    ai->RequestQuestForce(group_ai_refresh::PackForce(questId, objIdx));
    forced = true;
}

void WorldThreadManager::StageRelease(std::uint32_t groupId, Member const& m)
{
    StagedRelease r;
    r.groupId = groupId;
    r.member = m;
    releasing.push_back(r);
    staged.insert(m.guid);
}

void WorldThreadManager::StageForm(std::map<std::uint32_t, Player*> const& bots, std::vector<Candidate> const& members,
    std::uint32_t questId, std::uint32_t now)
{
    Player* leader = Find(bots, members[0].guid);
    if (!leader || !Eligible(leader) || leader->GetGroup())
        return;
    StagedForm f;
    f.questId = questId;
    for (Candidate const& c : members)
    {
        f.invitees.push_back(c.guid);
        staged.insert(c.guid);
    }
    f.leader.guid = members[0].guid;
    f.lo = f.hi = leader->GetLevel();
    forming.push_back(std::move(f));
    (void)now;
}

// One group operation (StagedOps): a queued release first, else the next step of the oldest formation - one invitee's
// invite and accept, or the setup once all were asked.
bool WorldThreadManager::StepOne(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)
{
    minute.queuedMax = std::max(minute.queuedMax, std::uint32_t(forming.size() + releasing.size()));
    std::uint32_t const ops0 = minute.ops;
    if (!releasing.empty())
    {
        StagedRelease const r = releasing.front();
        releasing.pop_front();
        staged.erase(r.member.guid);
        if (Player* bot = Find(bots, r.member.guid))
        {
            auto const t0 = std::chrono::steady_clock::now();
            Group* group = bot->GetGroup();
            group_op_timing::lastLeaveCoreUs = 0;
            Release(bot, r.member, group && group->GetId() == r.groupId);
            minute.leaveCoreUsMax = std::max(minute.leaveCoreUsMax, group_op_timing::lastLeaveCoreUs);
            minute.releaseUsMax = std::max(minute.releaseUsMax, UsSince(t0));
            ++minute.ops;
        }
        return ops0 != minute.ops;   // a release ran (its bot may have logged out meanwhile)
    }
    if (forming.empty())
        return false;

    StagedForm& f = forming.front();
    Player* leader = Find(bots, f.invitees[0]);
    Group* leaderGroup = leader ? leader->GetGroup() : nullptr;
    bool const leaderOk = leader && Eligible(leader) &&
        (f.groupId ? leaderGroup && leaderGroup->GetId() == f.groupId : !leaderGroup);
    if (!leaderOk)
    {
        // The leader went away (logout, instance, another group): whoever joined leaves again, one per update.
        for (Member const& m : f.joined)
            StageRelease(f.groupId, m);
        if (leader && leaderGroup && f.groupId && leaderGroup->GetId() == f.groupId)
            StageRelease(f.groupId, f.leader);
        else
            staged.erase(f.invitees[0]);
        for (std::size_t i = f.next; i < f.invitees.size(); ++i)
            staged.erase(f.invitees[i]);
        forming.pop_front();
        return false;
    }

    if (f.next < f.invitees.size())
    {
        std::uint32_t const guid = f.invitees[f.next++];
        Player* bot = Find(bots, guid);
        if (!bot || !Eligible(bot) || bot->GetGroup())
        {
            staged.erase(guid);
            return false;
        }
        if (f.joined.empty())
            f.leader.travel = SaveTravel(Target(Ai(leader)));   // before anything changes it
        Member m;
        m.guid = guid;
        m.travel = SaveTravel(Target(Ai(bot)));
        std::uint32_t inviteUs = 0;
        std::uint32_t acceptUs = 0;
        group_op_timing::lastAcceptCoreUs = 0;
        bool const joined = InviteAccept(leader, bot, &inviteUs, &acceptUs);
        minute.acceptCoreUsMax = std::max(minute.acceptCoreUsMax, group_op_timing::lastAcceptCoreUs);
        minute.inviteUsMax = std::max(minute.inviteUsMax, inviteUs);
        minute.acceptUsMax = std::max(minute.acceptUsMax, acceptUs);
        ++minute.ops;
        if (!joined)
        {
            staged.erase(guid);
            return true;
        }
        if (!f.groupId && leader->GetGroup())
            f.groupId = leader->GetGroup()->GetId();
        f.lo = std::min(f.lo, bot->GetLevel());
        f.hi = std::max(f.hi, bot->GetLevel());
        if (Cohort(guid) != Cohort(f.invitees[0]))
            f.mixed = true;
        f.joined.push_back(m);
        return true;
    }

    auto const t0 = std::chrono::steady_clock::now();
    FinishForm(bots, f, now);
    minute.setupUsMax = std::max(minute.setupUsMax, UsSince(t0));
    ++minute.ops;
    forming.pop_front();
    return true;
}

// The setup of a staged formation once every invitee was asked (FormOne's tail; the travel force is posted).
void WorldThreadManager::FinishForm(std::map<std::uint32_t, Player*> const& bots, StagedForm& f, std::uint32_t now)
{
    for (std::uint32_t guid : f.invitees)
        staged.erase(guid);
    Player* leader = Find(bots, f.invitees[0]);
    Group* group = leader ? leader->GetGroup() : nullptr;
    if (f.joined.empty() || !group || group->GetId() != f.groupId)
    {
        for (Member const& m : f.joined)
            StageRelease(f.groupId, m);
        if (group && group->GetId() == f.groupId)
            StageRelease(f.groupId, f.leader);   // a group of one is no group (#301)
        return;
    }

    HelpGroup g;
    g.groupId = f.groupId;
    g.questId = f.questId;
    g.leader = f.invitees[0];
    g.mapId = leader->GetMapId();
    g.formedAt = now;
    g.phaseSince = now;
    g.lastProgress = now;
    g.cohort = Cohort(g.leader);
    g.mixed = f.mixed;

    group->SetLootMethod(GROUP_LOOT);
    group->SetLootThreshold(ITEM_QUALITY_UNCOMMON);
    group->SendUpdate();

    Member lm = f.leader;
    lm.lastCounter = Counter(leader, f.questId);
    g.members.push_back(lm);
    Ai(leader)->SetQuestGroupRole(2);
    Ai(leader)->SetQuestGroupLeaderStuck(false);
    memberOf[g.leader] = g.groupId;
    std::vector<Player*> present{ leader };
    for (Member m : f.joined)
    {
        Player* bot = Find(bots, m.guid);
        if (!bot)
            continue;   // Tend sees it gone on the next pass
        Ai(bot)->SetQuestGroupRole(1);
        MakeFollower(Ai(bot), leader);
        m.lastCounter = Counter(bot, f.questId);
        memberOf[m.guid] = g.groupId;
        g.members.push_back(m);
        present.push_back(bot);
    }

    int const idx = OpenObjectiveIndex(present, f.questId);
    bool forced = false;
    if (idx >= 0)
        Force(leader, f.questId, idx, forced);
    if (forced)
        ++g.forces;

    sLog.outBasic("[QuestGroup] state=formed group=%u quest=%u size=%u leader=%u levels=%u-%u objective=%d cohort=%c mixed=%u",
        g.groupId, f.questId, uint32(g.members.size()), g.leader, f.lo, f.hi, idx, g.cohort, g.mixed ? 1u : 0u);
    groups[g.groupId] = std::move(g);
    ++minute.formed;
}

bool WorldThreadManager::FormOne(std::map<std::uint32_t, Player*> const& bots, std::vector<Candidate> const& members,
    std::uint32_t questId, std::uint32_t now)
{
    Player* leader = Find(bots, members[0].guid);
    if (!leader || !Eligible(leader) || leader->GetGroup())
        return false;

    HelpGroup g;
    g.questId = questId;
    g.leader = members[0].guid;
    g.mapId = leader->GetMapId();
    g.formedAt = now;
    g.phaseSince = now;
    g.lastProgress = now;
    g.cohort = Cohort(g.leader);

    // Save the own travel before anything changes it (the accept action resets the AI).
    Member lm;
    lm.guid = g.leader;
    lm.travel = SaveTravel(Target(Ai(leader)));
    std::vector<Member> joined;
    std::uint32_t lo = leader->GetLevel();
    std::uint32_t hi = leader->GetLevel();
    for (std::size_t i = 1; i < members.size(); ++i)
    {
        Player* bot = Find(bots, members[i].guid);
        if (!bot || !Eligible(bot) || bot->GetGroup())
            continue;
        Member m;
        m.guid = members[i].guid;
        m.travel = SaveTravel(Target(Ai(bot)));
        if (!InviteAccept(leader, bot))
            continue;
        joined.push_back(m);
        lo = std::min(lo, bot->GetLevel());
        hi = std::max(hi, bot->GetLevel());
        if (Cohort(m.guid) != g.cohort)
            g.mixed = true;
    }

    Group* group = leader->GetGroup();
    if (joined.empty() || !group)
    {
        if (group)
            LeaveCore(leader);   // a group of one is no group (#301)
        return false;
    }

    g.groupId = group->GetId();
    // Owner/OB-20 (#601): group loot from uncommon with the #341 roll code - the core default, set explicitly.
    group->SetLootMethod(GROUP_LOOT);
    group->SetLootThreshold(ITEM_QUALITY_UNCOMMON);
    group->SendUpdate();

    g.members.push_back(lm);
    Ai(leader)->SetQuestGroupRole(2);
    Ai(leader)->SetQuestGroupLeaderStuck(false);
    memberOf[g.leader] = g.groupId;
    for (Member& m : joined)
    {
        Player* bot = Find(bots, m.guid);
        Ai(bot)->SetQuestGroupRole(1);
        MakeFollower(Ai(bot), leader);
        m.lastCounter = Counter(bot, questId);
        memberOf[m.guid] = g.groupId;
        g.members.push_back(m);
    }
    g.members[0].lastCounter = Counter(leader, questId);

    std::vector<Player*> present{ leader };
    for (Member const& m : joined)
        present.push_back(Find(bots, m.guid));
    int const idx = OpenObjectiveIndex(present, questId);
    if (idx >= 0 && ForceQuestTravel(leader, questId, idx))
        ++g.forces;

    sLog.outBasic("[QuestGroup] state=formed group=%u quest=%u size=%u leader=%u levels=%u-%u objective=%d cohort=%c mixed=%u",
        g.groupId, questId, uint32(g.members.size()), g.leader, lo, hi, idx, g.cohort, g.mixed ? 1u : 0u);
    groups[g.groupId] = std::move(g);
    ++minute.formed;
    return true;
}

// The quest logs of the bots round.guids[round.next, end) - with one slice all bots of the scan, in map order.
void WorldThreadManager::ScanBots(std::map<std::uint32_t, Player*> const& bots, std::size_t end, std::uint32_t now)
{
    std::uint32_t const mode = sPlayerbotAIConfig.questGroupsEnable;
    // s7r (QuestGroups.ScanBudgetUs): the slice also ends once it took the budget; the rest goes to the next update.
    std::uint32_t const budgetUs = sPlayerbotAIConfig.questGroupsScanBudgetUs;
    auto const sliceStart = std::chrono::steady_clock::now();
    std::size_t const first = round.next;
    for (; round.next < end; ++round.next)
    {
        if (scan_slice::BudgetSpent(budgetUs, budgetUs ? UsSince(sliceStart) : 0u, round.next - first))
            break;
        std::uint32_t const guid = round.guids[round.next];
        Player* bot = Find(bots, guid);
        if (!bot || !Applies(mode, guid) || memberOf.count(guid) || staged.count(guid) || !Eligible(bot) || bot->GetGroup())
            continue;
        PlayerbotAI* ai = Ai(bot);
        TravelTarget* t = Target(ai);
        if (t->GetBandTripEnd())
            continue;   // a running S5a band trip is not interrupted (CLI-Deep 23.10.)
        for (std::uint16_t slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            std::uint32_t const questId = bot->GetQuestSlotQuestId(slot);
            Quest const* q = questId ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
            if (!q || !IsGroupHelpQuest(q->GetType(), q->GetSuggestedPlayers(), sPlayerbotAIConfig.questGroupsSuggestedPlayers))
                continue;
            QuestStatusData const* st = bot->GetQuestStatusData(questId);
            bool drop = false;
            QuestFacts f;
            f.incomplete = st && !st->m_rewarded && st->m_status == QUEST_STATUS_INCOMPLETE;
            f.classQuest = q->GetRequiredClasses() != 0;
            f.botLevel = bot->GetLevel();
            f.questLevel = q->GetQuestLevel() > 0 ? std::uint32_t(q->GetQuestLevel()) : f.botLevel;
            f.abandoned = t->IsStructurallyAbandoned(questId);
            f.openObjectives = OpenObjectives(bot, questId, &drop);
            if (!Waits(f))
                continue;
            Queue().Seen(guid, questId, now);
            if (Queue().Blocked(guid, questId, now) || Queue().Attempts(guid, questId) >= sPlayerbotAIConfig.questGroupsMaxAttempts)
                continue;
            Candidate c;
            c.guid = guid;
            c.level = bot->GetLevel();
            c.guild = bot->GetGuildId();
            c.openObjectives = f.openObjectives;
            c.x = bot->GetPositionX();
            c.y = bot->GetPositionY();
            std::uint64_t const key = (std::uint64_t(bot->GetMapId()) << 40) ^ (std::uint64_t(bot->GetTeam() == ALLIANCE ? 1 : 0) << 39) ^ questId;
            round.buckets[key].push_back(c);
            if (drop)
                round.dropOnly[key] = true;
            ++minute.candidates;
        }
    }
}

// The formations of a finished scan (FormOne checks each bot again: a slice may be up to ScanSlices updates old).
void WorldThreadManager::FormRound(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)
{
    FormLimits lim;
    lim.levelWindow = sPlayerbotAIConfig.questGroupsLevelWindow;
    lim.recruitRadius = sPlayerbotAIConfig.questGroupsRecruitRadius;
    for (auto& [key, pool] : round.buckets)
    {
        if (pool.size() < 2)
            continue;
        ++minute.quests;
        lim.maxSize = round.dropOnly.count(key) ? sPlayerbotAIConfig.questGroupsMaxSizeDrop : sPlayerbotAIConfig.questGroupsMaxSize;
        std::uint32_t const questId = std::uint32_t(key & 0xFFFFFFFFu);
        for (std::vector<Candidate> const& members : Form(pool, lim))
        {
            // A bot formed into one group this pass is not a candidate of another quest's group.
            bool taken = false;
            for (Candidate const& c : members)
                taken = taken || memberOf.count(c.guid) || staged.count(c.guid);
            if (taken)
                continue;
            auto const formStart = std::chrono::steady_clock::now();
            if (Staged())
                StageForm(bots, members, questId, now);   // StagedOps: queued, one member per update (StepOne)
            else
                FormOne(bots, members, questId, now);
            std::uint32_t const formUs = UsSince(formStart);
            minute.formUsMax = std::max(minute.formUsMax, formUs);
            passFormUs += formUs;
        }
    }
    Queue().ForgetStale(now, 6 * 3600);
}

void WorldThreadManager::FlushMinute(std::uint32_t now)
{
    if (!minute.start)
        minute.start = now;
    if (now - minute.start < 60)
        return;
    sLog.outBasic("[QuestGroup] minute candidates=%u quests=%u formed=%u active=%u dissolved=%u queued=%u scan_us_max=%u passes=%u mode=%u form_us_max=%u dissolve_us_max=%u scan_only_us_max=%u bots=%u slices=%u"
        " staged=%u invite_us_max=%u accept_us_max=%u setup_us_max=%u release_us_max=%u ops=%u ops_queued_max=%u"
        " tend_us_max=%u accept_core_us_max=%u leave_core_us_max=%u stall_us_max=%u one_step=%u scan_budget_us=%u defer=%u"
        " dissolves_deferred=%u",
        minute.candidates, minute.quests, minute.formed, uint32(groups.size()), minute.dissolved, uint32(Queue().Size()),
        minute.scanUsMax, minute.passes, sPlayerbotAIConfig.questGroupsEnable, minute.formUsMax, minute.dissolveUsMax, minute.scanOnlyUsMax,
        minute.bots, minute.slices, Staged() ? 1u : 0u, minute.inviteUsMax, minute.acceptUsMax, minute.setupUsMax, minute.releaseUsMax,
        minute.ops, minute.queuedMax, minute.tendUsMax, minute.acceptCoreUsMax, minute.leaveCoreUsMax, minute.stallUsMax,
        sPlayerbotAIConfig.questGroupsOneHeavyStep ? 1u : 0u, sPlayerbotAIConfig.questGroupsScanBudgetUs,
        sPlayerbotAIConfig.botGroupsWorldThreadDefer ? 1u : 0u, minute.deferredDissolves);
    minute = Minute();
    minute.start = now;
}

void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)
{
    FlushMinute(now);
    bool const enabled = sPlayerbotAIConfig.questGroupsEnable != ModeOff;
    auto const start = std::chrono::steady_clock::now();
    std::uint64_t const cpuStart = thread_cpu::NowUs();   // s7r: stall control (ThreadCpuClock.h)
    // s7r (QuestGroups.OneHeavyStep): set by a dissolution or a group operation; with the switch the update does nothing heavy
    // after it (HeavyStepTaken).
    bool const oneStep = sPlayerbotAIConfig.questGroupsOneHeavyStep;
    bool heavy = false;

    // Every update: tend the running groups (cheap: only their members). Groups ended in an earlier update and carried over
    // (OneHeavyStep) come first and are not tended again.
    std::vector<std::pair<std::uint32_t, Dissolve>> ended;
    ended.swap(dissolveLater);
    std::unordered_set<std::uint32_t> carried;
    for (auto const& [id, d] : ended)
        carried.insert(id);
    auto const tendStart = std::chrono::steady_clock::now();
    for (auto& [id, g] : groups)
    {
        if (carried.count(id))
            continue;
        Dissolve d = Dissolve::None;
        Tend(bots, g, now, d);
        if (d != Dissolve::None)
            ended.emplace_back(id, d);
    }
    minute.tendUsMax = std::max(minute.tendUsMax, UsSince(tendStart));
    for (auto const& [id, d] : ended)
    {
        auto const it = groups.find(id);
        if (it == groups.end())
            continue;
        if (HeavyStepTaken(oneStep, heavy))
        {
            dissolveLater.emplace_back(id, d);   // its own update next time
            ++minute.deferredDissolves;
            continue;
        }
        auto const dissolveStart = std::chrono::steady_clock::now();
        DissolveGroup(bots, it->second, d, now);
        minute.dissolveUsMax = std::max(minute.dissolveUsMax, UsSince(dissolveStart));
        groups.erase(it);
        heavy = true;
    }

    // StagedOps: at most one group operation per update (also with Enable 0: queued releases still run).
    if (!HeavyStepTaken(oneStep, heavy) && (!forming.empty() || !releasing.empty()))
        heavy = StepOne(bots, now);

    // Every ScanSeconds: the quest-log index and new groups (twow-repo#628 ScanSlices: spread over several updates).
    if (!enabled)
        round = ScanRound();
    else if (!round.active && now - lastScan >= sPlayerbotAIConfig.questGroupsScanSeconds)
    {
        lastScan = now;
        round = ScanRound();
        round.active = true;
        round.guids.reserve(bots.size());
        for (auto const& [guid, bot] : bots)
            round.guids.push_back(guid);
        minute.bots = std::max(minute.bots, std::uint32_t(bots.size()));
    }
    // OneHeavyStep: a running scan waits for an update without a dissolution or a group operation.
    if (round.active && !HeavyStepTaken(oneStep, heavy))
    {
        passFormUs = 0;
        auto const scanStart = std::chrono::steady_clock::now();
        ScanBots(bots, scan_slice::SliceEnd(round.guids.size(), round.next, sPlayerbotAIConfig.questGroupsScanSlices), now);
        ++round.updates;
        if (round.next >= round.guids.size())
        {
            FormRound(bots, now);
            ++minute.passes;
            minute.slices = round.updates;
            round = ScanRound();
        }
        std::uint32_t const scanUs = UsSince(scanStart);
        minute.scanOnlyUsMax = std::max(minute.scanOnlyUsMax, scanUs > passFormUs ? scanUs - passFormUs : 0u);
    }
    std::uint32_t const wallUs = UsSince(start);
    minute.scanUsMax = std::max(minute.scanUsMax, wallUs);
    minute.stallUsMax = std::max(minute.stallUsMax, thread_cpu::StallUs(wallUs, cpuStart, thread_cpu::NowUs()));
}

WorldThreadManager& WorldManager()
{
    static WorldThreadManager manager;
    return manager;
}
}
