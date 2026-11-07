#include "playerbot/playerbot.h"
#include "playerbot/GroupOps.h"
#include "playerbot/AdhocGroupWorld.h"
#include "playerbot/BotGroupDiagnostics.h"
#include "playerbot/GroupAiRefresh.h"
#include "playerbot/DeferredLog.h"
#include "playerbot/GroupOpTiming.h"
#include "playerbot/ThreadCpuClock.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "Group/Group.h"
#include "ScriptObjects.h"
#include "ScriptMgr.h"
#include "ObjectAccessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

// BOT-12 (twow-repo#677, #628; OB-00 04.11.2026; AiPlayerbot.BotGroups.OpQueue): the world-thread side of the group-operation
// queue. Design: cli-grp/crossmap/DESIGN-GROUPOPS-WORLDTHREAD-20261104.md sections 3 and 5 (PR1).
namespace ai::group_ops
{
namespace
{
using Clock = std::chrono::steady_clock;

// Milliseconds since the first call (any thread); differences are taken modulo 2^32.
std::uint32_t NowMs()
{
    static Clock::time_point const start = Clock::now();
    return std::uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

std::uint32_t UsSince(Clock::time_point start)
{
    return std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

// Seq of the next op, set before the push so the pending marks exist before the world thread can see the op.
std::atomic<std::uint32_t> nextSeq{ 1 };

std::uint32_t NextSeq()
{
    std::uint32_t seq = nextSeq.fetch_add(1, std::memory_order_relaxed);
    return seq ? seq : nextSeq.fetch_add(1, std::memory_order_relaxed);
}

// ---- off-thread observer (OB-00 04.11. addition b) -------------------------------------------------------------------------
// The world thread marks itself in every Drain (PlayerbotWorldScript::OnUpdate). That is the thread that also runs
// World::UpdateSessions, where the core handles the players' group opcodes (PACKET_PROCESS_GROUP = PACKET_PROCESS_WORLD).
// Call chain of the drain: World::Update -> UpdateSessions (World.cpp:2996) -> sMapMgr.Update (:3038; continents.wait() and
// the instance f.wait() loop, then the teleports) -> WORLDHOOK_ON_UPDATE (:3310) -> PlayerbotWorldScript::OnUpdate ->
// sRandomPlayerbotMgr.UpdateAI -> group_ops::Drain: the maps are idle. An instance updated inline on the world thread (no
// instance pool; MapManager::Update collection loop, before the continents are dispatched) is serial, so on-thread is right
// there too. Any other thread is a map/region thread, a command thread or another module's thread - "off the world thread".
thread_local bool t_worldThread = false;
std::atomic<bool> worldSeen{ false };

enum GroupEvent : std::size_t
{
    EvCreate = 0,
    EvDisband,
    EvAdd,
    EvRemove,
    EvLeader,
    EvInvite,
    kEvents,
};

struct OffCounts
{
    std::array<std::atomic<std::uint32_t>, kEvents> off{};
    std::array<std::atomic<std::uint32_t>, kOffSources> bySource{};    // creates + disbands (m_GroupMap writes), every bot
    std::array<std::atomic<std::uint32_t>, kOffSources> byQueueBot{};  // creates + disbands + adds + removes by a queue bot
    std::atomic<std::uint32_t> real{ 0 };   // creates + disbands with a real player involved: the actor's master or the leader (PR3)
    std::atomic<std::uint32_t> wtCreate{ 0 };
    std::atomic<std::uint32_t> wtDisband{ 0 };
};

OffCounts offCounts;

// A3 lever 2 (BotGroups.WritePath): group events OFF the world thread caused by an in-scope bot - outside the "join"/"lfg"
// bypass scope (off_wp_<event>, the write path's primary metric: 0) and inside it (bypass). Counted only with the key on.
struct WriteCounts
{
    std::array<std::atomic<std::uint32_t>, kEvents> offWp{};
    std::atomic<std::uint32_t> bypass{ 0 };
    // OB-00 07.11.2026 (c): queued accepts of a real player's invite (deviation from ADR-0041 E7, measured on its own).
    std::atomic<std::uint32_t> acceptPlayer{ 0 };
};

WriteCounts writeCounts;

bool ObserverOn()
{
    // A3 lever 2: the observer (and so the drain's minute) also runs with BotGroups.WritePath, whatever OpQueue says.
    if (sPlayerbotAIConfig.botGroupsWritePath)
        return true;
    return sPlayerbotAIConfig.botGroupsDiagnostics || sPlayerbotAIConfig.botGroupsOpQueue;
}

// A3 lever 2: the game part of "in scope" (design 3): the cohort of the key, a bot AI without a real-player master (roster
// and free bots; a real player's bots keep the legacy path, ADR draft E7), not in a battleground (BG groups are out of scope).
bool InWritePathScope(PlayerbotAI* ai)
{
    Player* const bot = ai ? ai->GetBot() : nullptr;
    return bot && !ai->IsRealPlayer() && !ai->HasRealPlayerMaster() && !bot->InBattleGround() &&
        WritePathApplies(sPlayerbotAIConfig.botGroupsWritePath, bot->GetGUIDLow());
}

bool IsRealPlayerOrSelfBot(Player* player)
{
    PlayerbotAI* const ai = player ? GetBotAI(player) : nullptr;
    return player && (!ai || ai->IsRealPlayer());
}

// Counter only: relaxed atomics, no lock, no log line per event. addedLow: the member an EvAdd adds.
void Observe(GroupEvent e, Group* group, std::uint32_t addedLow = 0)
{
    if (!ObserverOn())
        return;
    bool const mapWrite = e == EvCreate || e == EvDisband;
    if (t_worldThread)
    {
        if (mapWrite)
            (e == EvCreate ? offCounts.wtCreate : offCounts.wtDisband).fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!worldSeen.load(std::memory_order_relaxed))
        return;   // before the first world tick: no world thread known yet
    offCounts.off[e].fetch_add(1, std::memory_order_relaxed);
    // A3 lever 2 (BotGroups.WritePath): every event (leader and invite too) whose outermost acting bot is in scope.
    if (sPlayerbotAIConfig.botGroupsWritePath)
    {
        PlayerbotAI* const wpActor = t_outerAction ? t_actionAi : nullptr;
        if (InWritePathScope(wpActor))
            (t_writeBypass ? writeCounts.bypass : writeCounts.offWp[e]).fetch_add(1, std::memory_order_relaxed);
    }
    if (!mapWrite && e != EvAdd && e != EvRemove)
        return;   // leader change / invite: totals only

    PlayerbotAI* const actor = t_outerAction ? t_actionAi : nullptr;
    Player* const actorBot = actor ? actor->GetBot() : nullptr;
    std::string const action = t_outerAction ? t_outerAction->getName() : std::string();
    // The old ad-hoc path creates the group in the invitee's own "accept invitation" (HandleGroupAcceptOpcode): an accept whose
    // group leader is the inviter the bot was tagged with is the ad-hoc path, not a PR2/PR3 accept.
    bool adhocInvite = false;
    if (actor && action == "accept invitation")
    {
        std::uint32_t const tag = actor->AdhocInviteFrom();
        adhocInvite = tag && tag == group->GetLeaderGuid().GetCounter();
        if (e == EvAdd && actorBot && addedLow == actorBot->GetGUIDLow())
            actor->TakeAdhocInviteFrom();   // the bot joined (Create adds the leader first, then OnCreate, then the bot)
    }
    OffSource const src = OffSourceOf(t_outerAction != nullptr, action, adhocInvite);
    if (mapWrite)
    {
        offCounts.bySource[std::size_t(src)].fetch_add(1, std::memory_order_relaxed);
        // A real player's own group opcodes run on the world thread; off it, a real player is involved through a bot with a
        // real-player master, or through a group a real player leads (a bot's first accept of a real player's invite: the
        // master is set only after the core accept).
        if ((actor && actor->HasRealPlayerMaster()) || IsRealPlayerOrSelfBot(sObjectMgr.GetPlayer(group->GetLeaderGuid())))
            offCounts.real.fetch_add(1, std::memory_order_relaxed);
    }
    if (actorBot && QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, actorBot->GetGUIDLow()))
        offCounts.byQueueBot[std::size_t(src)].fetch_add(1, std::memory_order_relaxed);
}

// The core's group hooks (Group::Create, Group::Disband, AddMember, RemoveMember, ChangeLeader, AddInvite;
// src/game/Group/Group.cpp) fire on the thread that changes the group. Create/Disband of a non-battleground group are the
// m_GroupMap writes (AddGroup after Create in HandleGroupAcceptOpcode, RemoveGroup after the disband in
// Player::RemoveFromGroup / UninviteFromGroup). No core hook was needed.
class GroupOpsObserver : public GroupScript
{
public:
    GroupOpsObserver() : GroupScript("PlayerbotGroupOpsObserver") {}

    void OnCreate(Group* group, ObjectGuid /*leaderGuid*/, uint8 /*groupType*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvCreate, group);
    }
    void OnInviteMember(Group* group, ObjectGuid /*guid*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvInvite, group);
    }
    void OnAddMember(Group* group, ObjectGuid guid) override
    {
        if (group && !group->isBGGroup())
            Observe(EvAdd, group, guid.GetCounter());
    }
    void OnRemoveMember(Group* group, ObjectGuid /*guid*/, uint8 /*method*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvRemove, group);
    }
    void OnChangeLeader(Group* group, ObjectGuid /*newLeaderGuid*/, ObjectGuid /*oldLeaderGuid*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvLeader, group);
    }
    void OnDisband(Group* group) override
    {
        if (group && !group->isBGGroup())
            Observe(EvDisband, group);
    }
};

// ---- world-thread state ------------------------------------------------------------------------------------------------------
struct DrainMinute
{
    std::array<std::uint32_t, kKinds> executed{};
    std::array<std::uint32_t, kKinds> failed{};     // ran, but the core refused (invite gone, accept refused, still grouped)
    std::array<std::uint32_t, kKinds> stale{};      // dropped by a staleness rule
    std::array<std::uint32_t, kVerdicts> verdicts{};
    std::uint32_t superseded = 0;
    std::uint32_t ageMsMax = 0;
    std::uint32_t ageHist[kAgeBuckets] = {};       // ages of the ops looked at (age_ms_p99)
    std::uint32_t drainUsMax = 0;
    std::uint32_t opUsMax = 0;
    std::uint32_t leaveCoreUsMax = 0;
    std::uint32_t joinCoreUsMax = 0;
    std::uint32_t stallUsMax = 0;
    std::uint32_t ticksWithOp = 0;
    std::uint32_t heavySkips = 0;
};

struct WorldState
{
    bool heavyTick = false;
    bool minuteStarted = false;
    std::uint32_t minuteStartMs = 0;
    bool alerted = false;
    std::uint32_t lastAlertMs = 0;
    bool alertChecked = false;
    std::uint32_t lastAlertCheckMs = 0;
    DrainMinute minute;
};

WorldState& WorldSide()
{
    static WorldState state;
    return state;
}

Player* FindPlayer(std::uint32_t guidLow)
{
    // Also a bot in a loading screen (not in the world, still logged in): its op waits (Retry), it is not "logged out".
    return guidLow ? ObjectAccessor::FindPlayerNotInWorld(ObjectGuid(HIGHGUID_PLAYER, guidLow)) : nullptr;
}

void ClearPending(std::uint32_t guidLow, std::uint32_t seq)
{
    if (Player* player = FindPlayer(guidLow))
        if (PlayerbotAI* ai = GetBotAI(player))
            ai->ClearGroupOpPending(seq);
}

void ClearPending(Op const& op)
{
    ClearPending(op.bot, op.seq);
    if (HoldsBoth(op.kind))
        ClearPending(op.other, op.seq);
}

// A3 lever 2 (review fix): an Accept op dropped or superseded must not leave its invite pending - the "group invite" trigger
// fired once, so nothing would answer it again and the bot would refuse every later invite until the inviter's group goes.
// Only the invite this op answered (its leader still op.other); world thread, as RunJoin's own cleanup.
void DropAcceptInvite(Op const& op)
{
    if (op.kind != OpKind::Accept)
        return;
    Player* bot = FindPlayer(op.bot);
    Group* invite = bot ? bot->GetGroupInvite() : nullptr;
    if (invite && invite->GetLeaderGuid().GetCounter() == op.other)
        bot->UninviteFromGroup();
}

// The core leave of the ad-hoc manager (WorldThreadManager::LeaveGroup, S7 LeaveCore): HandleGroupDisbandOpcode ->
// RemoveFromGroup -> RemoveMember, or Disband + RemoveGroup + delete for a group of two. With the maps idle. The log lines and
// the AI part (strategy reset) go to the bot's own thread - always, whatever WorldThreadDefer says.
Verdict RunLeave(Op const& op, std::uint32_t transitMs, DrainMinute& m)
{
    Player* bot = FindPlayer(op.bot);
    PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
    LeaveOpFacts f;
    f.found = bot && ai && bot->GetSession();
    if (f.found)
    {
        f.inWorld = bot->IsInWorld();
        f.teleporting = bot->IsBeingTeleported();
        f.realMaster = ai->HasRealPlayerMaster();
        f.inBattleground = bot->InBattleGround();
        Group* now = bot->GetGroup();
        f.groupId = now ? now->GetId() : 0u;
    }
    f.opGroupId = op.groupId;
    f.transitMs = transitMs;
    Verdict const v = DecideLeaveOp(f);
    if (v != Verdict::Execute)
        return v;

    Group* group = bot->GetGroup();
    // The ad-hoc registry: the group ends with the second-to-last member (core disband) - forgotten below, only once the bot is
    // really out (a refused core leave keeps the group ad-hoc, so the bot decides again).
    std::uint32_t const groupId = group->GetId();
    bool const lastPair = group->GetMembersCount() <= 2;
    // A3 lever 2 (review fix): the tail of the code the op replaces (LeaveTailOf): the ad-hoc tail as before; LeaveGroupAction's
    // own (Reset only with a bot master); none for RepopAction (its legacy raw RemoveMember had no line, master clear or reset).
    LeaveTail const tail = LeaveTailOf(op.reason);
    if (tail != LeaveTail::None)
    {
        deferred_log::Scope const deferLogs(ai, true);
        sPlayerbotAIConfig.logEvent(ai, "LeaveGroupAction", group->GetLeaderName(), std::to_string(group->GetMembersCount() - 1));
        bot_group::LogMembership(bot, group, "leave");
    }

    WorldPacket p;
    std::string member = bot->GetName();
    p << uint32(PARTY_OP_LEAVE) << member << uint32(0);
    auto const coreStart = Clock::now();
    bot->GetSession()->HandleGroupDisbandOpcode(p);
    m.leaveCoreUsMax = std::max(m.leaveCoreUsMax, group_op_timing::UsSince(coreStart));
    // `group` may be deleted now (a group of two): only the bot's own pointer from here on.

    if (tail != LeaveTail::None)
    {
        if (sRandomPlayerbotMgr.IsFreeBot(bot))
            ai->SetMaster(nullptr);
        if (tail == LeaveTail::CommandBotMaster)
            ai->RequestGroupAi(group_ai_refresh::LeaveResetOnly);
        else
            ai->RequestGroupAi(group_ai_refresh::LeaveReset);
    }

    Group* after = bot->GetGroup();
    if (after && after->GetId() == groupId)
    {
        ++m.failed[std::size_t(OpKind::Leave)];
        return v;
    }
    if (lastPair)
        adhoc_group::Groups().Forget(groupId);
    ++m.executed[std::size_t(OpKind::Leave)];
    return v;
}

// The core join of the ad-hoc manager (WorldThreadManager::Join): the inviter's invite opcode, then the invitee's
// "accept invitation" with the group-op source - the accept keeps only the core join here and posts its tail to the bot
// (AcceptInvitationAction, group_ai_refresh::GroupOpSource) - then the ad-hoc registry and free-for-all loot.
Verdict RunJoin(Op const& op, std::uint32_t transitMs, DrainMinute& m)
{
    Player* invitee = FindPlayer(op.bot);
    Player* inviter = FindPlayer(op.other);
    PlayerbotAI* inviteeAi = invitee ? GetBotAI(invitee) : nullptr;
    PlayerbotAI* inviterAi = inviter ? GetBotAI(inviter) : nullptr;
    JoinOpFacts f;
    f.found = inviteeAi && inviterAi && invitee->GetSession() && inviter->GetSession();
    if (f.found)
    {
        f.inWorld = invitee->IsInWorld() && inviter->IsInWorld();
        f.teleporting = invitee->IsBeingTeleported() || inviter->IsBeingTeleported();
        f.realMaster = inviteeAi->HasRealPlayerMaster() || inviterAi->HasRealPlayerMaster();
        f.inBattleground = invitee->InBattleGround() || inviter->InBattleGround();
        f.inviteeGrouped = invitee->GetGroup() != nullptr;
        f.inviteeInvited = invitee->GetGroupInvite() != nullptr;
        Group* g = inviter->GetGroup();
        f.inviterGroupId = g ? g->GetId() : 0u;
        f.inviterLeads = g && g->IsLeader(inviter->GetObjectGuid());
        f.inviterMembers = g ? g->GetMembersCount() : 1u;
        f.maxMembers = sPlayerbotAIConfig.botGroupsMaxBots;
        f.sameMap = invitee->GetMapId() == inviter->GetMapId();
        f.inRange = f.inWorld && f.sameMap &&
            sServerFacade.GetDistance2d(inviter, invitee) <= 2.0f * sPlayerbotAIConfig.botGroupsAdhocRadius;
    }
    f.opGroupId = op.groupId;
    f.transitMs = transitMs;
    Verdict const v = DecideJoinOp(f);
    if (v != Verdict::Execute)
        return v;

    // The existing invite path; the core refuses a full group or a pending invite.
    WorldPacket p;
    uint32 const rolesMask = 0;
    p << invitee->GetName();
    p << rolesMask;
    inviter->GetSession()->HandleGroupInviteOpcode(p);
    if (!invitee->GetGroupInvite())
    {
        ++m.failed[std::size_t(OpKind::Join)];
        return v;
    }

    group_op_timing::lastAcceptCoreUs = 0;
    inviteeAi->DoSpecificAction("accept invitation", Event(group_ai_refresh::GroupOpSource, "", inviter), true);
    m.joinCoreUsMax = std::max(m.joinCoreUsMax, group_op_timing::lastAcceptCoreUs);

    Group* group = invitee->GetGroup();
    if (!group || !group->IsMember(inviter->GetObjectGuid()))
    {
        if (invitee->GetGroupInvite())
            invitee->UninviteFromGroup();
        ++m.failed[std::size_t(OpKind::Join)];
        return v;
    }

    adhoc_group::Registry::Entry entry;
    if (!adhoc_group::Groups().Find(group->GetId(), entry) && adhoc_group::AllRosterBots(group))
    {
        adhoc_group::ObjectiveKey key;
        key.questId = op.questId;
        key.objective = op.objective;
        adhoc_group::Groups().Register(group->GetId(), key, uint32(time(nullptr)));
        // Design section 4 (#365): free-for-all, so there is no roll delay on quest items.
        group->SetLootMethod(FREE_FOR_ALL);
        group->SendUpdate();
        if (sPlayerbotAIConfig.botGroupsDiagnostics)
        {
            // Written by the invitee on its own thread (DeferredLog), like the accept's own lines.
            char line[256];
            snprintf(line, sizeof(line), "[BotGroup] kind=adhoc event=register bot=%u group=%u quest=%u objective=%u members=%u thread=world via=queue",
                invitee->GetGUIDLow(), group->GetId(), key.questId, uint32(key.objective), group->GetMembersCount());
            inviteeAi->PostDeferredLog("", line);
        }
    }
    ++m.executed[std::size_t(OpKind::Join)];
    return v;
}

// A3 lever 2 (BotGroups.WritePath): the invitee's accept (AcceptInvitationAction) as an op. Re-checked here with the maps idle,
// then the accept runs exactly as in RunJoin: the action with the group-op source keeps the core join (HandleGroupAcceptOpcode,
// SetMaster) and posts its tail to the bot (RequestJoinTail). The security decline is the same op with kAcceptDecline: the
// decline packet and UninviteFromGroup, here instead of on the bot's map thread.
Verdict RunAccept(Op const& op, std::uint32_t transitMs, DrainMinute& m)
{
    Player* bot = FindPlayer(op.bot);
    PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
    Player* inviter = FindPlayer(op.other);
    AcceptOpFacts f;
    f.found = bot && ai && bot->GetSession() && inviter && inviter->GetSession();
    if (f.found)
    {
        f.inWorld = bot->IsInWorld() && inviter->IsInWorld();
        f.teleporting = bot->IsBeingTeleported() || inviter->IsBeingTeleported();
        f.realMaster = ai->HasRealPlayerMaster();
        Group* invite = bot->GetGroupInvite();
        f.invited = invite && invite->GetLeaderGuid().GetCounter() == op.other;
    }
    f.transitMs = transitMs;
    Verdict const v = DecideAcceptOp(f);
    if (v != Verdict::Execute)
        return v;

    std::size_t const k = std::size_t(OpKind::Accept);
    if (op.reason == kAcceptDecline)
    {
        WorldPacket data(SMSG_GROUP_DECLINE, 10);
        data << bot->GetName();
        sServerFacade.SendPacket(inviter, data);
        bot->UninviteFromGroup();
        ++(bot->GetGroupInvite() ? m.failed[k] : m.executed[k]);
        return v;
    }

    ai->DoSpecificAction("accept invitation", Event(group_ai_refresh::GroupOpSource, "", inviter), true);
    Group* group = bot->GetGroup();
    ++(group && group->IsMember(inviter->GetObjectGuid()) ? m.executed[k] : m.failed[k]);
    return v;
}

// A3 lever 2: InviteToGroupAction::Invite as an op - its core steps here: the raid conversion where the legacy code does it
// (a bot invitee into a party of 5, or the caller's "5th member" rule carried as kInviteRaidConvert), then the inviter's
// invite opcode. No tail: the invitee's own "accept invitation" answers the SMSG_GROUP_INVITE (in scope: an Accept op).
Verdict RunInvite(Op const& op, std::uint32_t transitMs, DrainMinute& m)
{
    Player* inviter = FindPlayer(op.bot);
    PlayerbotAI* inviterAi = inviter ? GetBotAI(inviter) : nullptr;
    Player* invitee = FindPlayer(op.other);
    InviteOpFacts f;
    bool convert = false;
    f.found = inviterAi && inviter->GetSession() && invitee && invitee->GetSession();
    if (f.found)
    {
        f.inWorld = inviter->IsInWorld() && invitee->IsInWorld();
        f.teleporting = inviter->IsBeingTeleported() || invitee->IsBeingTeleported();
        f.realMaster = inviterAi->HasRealPlayerMaster();
        f.inBattleground = inviter->InBattleGround() || invitee->InBattleGround();
        f.inviteeGrouped = invitee->GetGroup() != nullptr;
        f.inviteeInvited = invitee->GetGroupInvite() != nullptr;
        Group* g = inviter->GetGroup();
        f.inviterGroupId = g ? g->GetId() : 0u;
        PlayerbotAI* inviteeAi = GetBotAI(invitee);
        convert = g && !g->IsRaidGroup() &&
            ((op.reason == kInviteRaidConvert && g->GetMembersCount() > 3) ||
             (inviteeAi && !inviteeAi->IsRealPlayer() && g->GetMembersCount() > 4));
        f.groupFull = g && !convert && g->IsFull();
    }
    f.opGroupId = op.groupId;
    f.transitMs = transitMs;
    Verdict const v = DecideInviteOp(f);
    if (v != Verdict::Execute)
        return v;

    if (convert)
        inviter->GetGroup()->ConvertToRaid();
    WorldPacket p;
    uint32 const rolesMask = 0;
    p << invitee->GetName();
    p << rolesMask;
    inviter->GetSession()->HandleGroupInviteOpcode(p);
    ++(invitee->GetGroupInvite() ? m.executed[std::size_t(OpKind::Invite)] : m.failed[std::size_t(OpKind::Invite)]);
    return v;
}

// A3 lever 2: PassLeadershipToMasterAction's core call as an op (HandleGroupSetLeaderOpcode on the leader's session); its
// strategy reset (a random bot) goes back to the bot (LeaderReset). The producer already told the new leader.
Verdict RunLeader(Op const& op, std::uint32_t transitMs, DrainMinute& m)
{
    Player* bot = FindPlayer(op.bot);
    PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
    Player* newLeader = FindPlayer(op.other);
    LeaderOpFacts f;
    f.found = bot && ai && bot->GetSession() && newLeader && newLeader->GetSession();
    if (f.found)
    {
        f.inWorld = bot->IsInWorld() && newLeader->IsInWorld();
        f.teleporting = bot->IsBeingTeleported() || newLeader->IsBeingTeleported();
        f.realMaster = ai->HasRealPlayerMaster();
        f.inBattleground = bot->InBattleGround();
        Group* g = bot->GetGroup();
        f.groupId = g ? g->GetId() : 0u;
        f.leads = g && g->IsLeader(bot->GetObjectGuid());
        f.otherMember = g && newLeader->GetGroup() == g;
    }
    f.opGroupId = op.groupId;
    f.transitMs = transitMs;
    Verdict const v = DecideLeaderOp(f);
    if (v != Verdict::Execute)
        return v;

    WorldPacket p(SMSG_GROUP_SET_LEADER, 8);
    p << newLeader->GetObjectGuid();
    bot->GetSession()->HandleGroupSetLeaderOpcode(p);
    // As the legacy code: the random bot's reset after the core call, whether the core changed the leader or not.
    if (op.reason == kLeaderReset)
        ai->RequestGroupAi(group_ai_refresh::LeaderReset);
    Group* g = bot->GetGroup();
    if (!g || !g->IsLeader(newLeader->GetObjectGuid()))
    {
        ++m.failed[std::size_t(OpKind::Leader)];
        return v;
    }
    ++m.executed[std::size_t(OpKind::Leader)];
    return v;
}

Verdict Run(Op const& op, std::uint32_t transitMs, DrainMinute& m)
{
    switch (op.kind)
    {
        case OpKind::Leave:  return RunLeave(op, transitMs, m);
        case OpKind::Join:   return RunJoin(op, transitMs, m);
        case OpKind::Accept: return RunAccept(op, transitMs, m);
        case OpKind::Invite: return RunInvite(op, transitMs, m);
        case OpKind::Leader: return RunLeader(op, transitMs, m);
    }
    return Verdict::DropLoggedOut;
}

void AddField(std::string& out, char const* name, std::uint32_t value)
{
    out += ' ';
    out += name;
    out += '=';
    out += std::to_string(value);
}

std::uint32_t Take(std::atomic<std::uint32_t>& counter)
{
    return counter.exchange(0, std::memory_order_relaxed);
}

// A3 lever 2: <prefix><kind> for every kind, in OpKind order (leave, join, accept, invite, leader).
void AddKindFields(std::string& out, char const* prefix, std::array<std::uint32_t, kKinds> const& values)
{
    for (std::size_t i = 0; i < kKinds; ++i)
        AddField(out, (std::string(prefix) + KindName(OpKind(i))).c_str(), values[i]);
}

// One line per minute (world thread), with BotGroups.Diagnostics or OpQueue on; counters are increments of this minute.
void FlushMinute(WorldState& w, std::uint32_t nowMs)
{
    if (!w.minuteStarted)
    {
        w.minuteStarted = true;
        w.minuteStartMs = nowMs;
        return;
    }
    if (nowMs - w.minuteStartMs < 60000)
        return;
    w.minuteStartMs = nowMs;

    PushCounts const pc = Ops().TakeCounts();
    DrainMinute const m = w.minute;
    w.minute = DrainMinute();
    std::array<std::uint32_t, kEvents> off{};
    for (std::size_t i = 0; i < kEvents; ++i)
        off[i] = Take(offCounts.off[i]);
    std::array<std::uint32_t, kOffSources> bySource{};
    std::array<std::uint32_t, kOffSources> byQueueBot{};
    for (std::size_t i = 0; i < kOffSources; ++i)
    {
        bySource[i] = Take(offCounts.bySource[i]);
        byQueueBot[i] = Take(offCounts.byQueueBot[i]);
    }
    std::uint32_t const real = Take(offCounts.real);
    std::uint32_t const wtCreate = Take(offCounts.wtCreate);
    std::uint32_t const wtDisband = Take(offCounts.wtDisband);
    if (!ObserverOn())
        return;

    std::size_t const L = std::size_t(OpKind::Leave);
    std::size_t const J = std::size_t(OpKind::Join);
    std::string line = "[GroupOps] minute";
    AddField(line, "mode", sPlayerbotAIConfig.botGroupsOpQueue);
    AddField(line, "ops_per_tick", sPlayerbotAIConfig.botGroupsOpQueueOpsPerTick);
    AddField(line, "max_depth", sPlayerbotAIConfig.botGroupsOpQueueMaxDepth);
    AddField(line, "alert_age_s", sPlayerbotAIConfig.botGroupsOpQueueAlertAgeSeconds);
    AddField(line, "enq_leave", pc.queued[L]);
    AddField(line, "enq_join", pc.queued[J]);
    AddField(line, "exec_leave", m.executed[L]);
    AddField(line, "exec_join", m.executed[J]);
    AddField(line, "fail_leave", m.failed[L]);
    AddField(line, "fail_join", m.failed[J]);
    AddField(line, "stale_leave", m.stale[L]);
    AddField(line, "stale_join", m.stale[J]);
    AddField(line, "dropped_full_leave", pc.droppedFull[L]);
    AddField(line, "dropped_full_join", pc.droppedFull[J]);
    AddField(line, "refused_pending", pc.refusedPending[L] + pc.refusedPending[J]);
    AddField(line, "superseding", pc.superseding);
    AddField(line, "superseded", m.superseded);
    for (std::size_t i = std::size_t(Verdict::Retry); i < kVerdicts; ++i)
        AddField(line, VerdictName(Verdict(i)), m.verdicts[i]);
    AddField(line, "depth", std::uint32_t(Ops().Depth()));
    AddField(line, "depth_max", pc.depthMax);
    AddField(line, "age_ms_max", m.ageMsMax);
    AddField(line, "age_ms_p99", AgeP99Ms(m.ageHist, m.ageMsMax));
    AddField(line, "drain_us_max", m.drainUsMax);
    AddField(line, "op_us_max", m.opUsMax);
    AddField(line, "leave_core_us_max", m.leaveCoreUsMax);
    AddField(line, "join_core_us_max", m.joinCoreUsMax);
    AddField(line, "stall_us_max", m.stallUsMax);
    AddField(line, "ticks_with_op", m.ticksWithOp);
    AddField(line, "heavy_skips", m.heavySkips);
    AddField(line, "off_create", off[EvCreate]);
    AddField(line, "off_disband", off[EvDisband]);
    AddField(line, "off_add", off[EvAdd]);
    AddField(line, "off_remove", off[EvRemove]);
    AddField(line, "off_leader", off[EvLeader]);
    AddField(line, "off_invite", off[EvInvite]);
    AddField(line, "off_map_real", real);
    for (std::size_t i = 0; i < kOffSources; ++i)
        AddField(line, ("off_map_" + std::string(OffSourceName(OffSource(i)))).c_str(), bySource[i]);
    for (std::size_t i = 0; i < kOffSources; ++i)
        AddField(line, ("off_q_" + std::string(OffSourceName(OffSource(i)))).c_str(), byQueueBot[i]);
    AddField(line, "wt_create", wtCreate);
    AddField(line, "wt_disband", wtDisband);
    sLog.outBasic("%s", line.c_str());

    // A3 lever 2 (BotGroups.WritePath): the write path's own line, only with the key on (the [GroupOps] line above keeps its
    // fields). The same minute's counts, every kind: leave, join, accept, invite, leader.
    if (!sPlayerbotAIConfig.botGroupsWritePath)
        return;
    std::string wline = "[GroupWrite] minute";
    AddField(wline, "mode", sPlayerbotAIConfig.botGroupsWritePath);
    AddKindFields(wline, "enq_", pc.queued);
    AddKindFields(wline, "exec_", m.executed);
    AddKindFields(wline, "fail_", m.failed);
    AddKindFields(wline, "stale_", m.stale);
    AddKindFields(wline, "refused_pending_", pc.refusedPending);
    AddKindFields(wline, "dropped_full_", pc.droppedFull);
    AddField(wline, "superseding", pc.superseding);
    AddField(wline, "superseded", m.superseded);
    AddField(wline, "bypass", Take(writeCounts.bypass));
    AddField(wline, "off_wp_create", Take(writeCounts.offWp[EvCreate]));
    AddField(wline, "off_wp_disband", Take(writeCounts.offWp[EvDisband]));
    AddField(wline, "off_wp_add", Take(writeCounts.offWp[EvAdd]));
    AddField(wline, "off_wp_remove", Take(writeCounts.offWp[EvRemove]));
    AddField(wline, "off_wp_leader", Take(writeCounts.offWp[EvLeader]));
    AddField(wline, "off_wp_invite", Take(writeCounts.offWp[EvInvite]));
    AddField(wline, "accept_player", Take(writeCounts.acceptPlayer));
    sLog.outBasic("%s", wline.c_str());
}
}

Queue& Ops()
{
    static Queue queue;
    return queue;
}

Pushed Enqueue(Op& op, PlayerbotAI* botAi, PlayerbotAI* otherAi)
{
    op.enqueuedMs = NowMs();
    op.seq = NextSeq();
    // The marks first: a map-thread producer never runs while the world thread drains, but a mark set after the push could be
    // set after the op ran and then never be cleared. A refused push restores the previous marks.
    std::uint32_t const oldBot = botAi ? botAi->ExchangeGroupOpPending(op.seq) : 0u;
    std::uint32_t const oldOther = otherAi ? otherAi->ExchangeGroupOpPending(op.seq) : 0u;
    Pushed const r = Ops().Push(op, sPlayerbotAIConfig.botGroupsOpQueueMaxDepth);
    if (r != Pushed::Queued)
    {
        if (botAi)
            botAi->RestoreGroupOpPending(op.seq, oldBot);
        if (otherAi)
            otherAi->RestoreGroupOpPending(op.seq, oldOther);
    }
    return r;
}

bool StillPending(PlayerbotAI* ai, std::uint32_t guidLow)
{
    std::uint32_t const seq = ai->GroupOpPendingSeq();
    if (!seq)
        return false;
    if (Ops().IsPending(guidLow))
        return true;
    ai->ClearGroupOpPending(seq);   // ran or dropped while the bot was not found: the queue is the truth; a newer mark survives
    return false;
}

bool PendingHint(PlayerbotAI* ai, std::uint32_t guidLow)
{
    return ai->GroupOpPending() && Ops().IsPending(guidLow);
}

void NoteAdhocInvite(PlayerbotAI* inviteeAi, std::uint32_t inviterGuidLow)
{
    if (inviteeAi && ObserverOn())
        inviteeAi->SetAdhocInviteFrom(inviterGuidLow);
}

void MarkHeavyTick()
{
    WorldSide().heavyTick = true;
}

void Drain()
{
    t_worldThread = true;
    worldSeen.store(true, std::memory_order_relaxed);
    WorldState& w = WorldSide();
    bool const heavy = w.heavyTick;
    w.heavyTick = false;
    Queue& queue = Ops();
    // Switch 0 (OpQueue off, no diagnostics) with nothing queued: no clock read, no lock.
    if (!queue.DepthHint() && !ObserverOn())
        return;
    std::uint32_t const nowMs = NowMs();

    std::size_t const depth = queue.Depth();
    if (depth)
    {
        DrainMinute& m = w.minute;
        std::uint32_t const budget = OpsThisTick(sPlayerbotAIConfig.botGroupsOpQueueOpsPerTick, heavy);
        if (!budget)
            ++m.heavySkips;
        else
        {
            auto const drainStart = Clock::now();
            std::uint64_t const cpuStart = thread_cpu::NowUs();
            std::uint32_t looks = LooksThisTick(budget);
            std::uint32_t ran = 0;
            std::vector<Op> later;   // in transit: back into the queue after this tick's loop (looked at once per tick)
            while (ran < budget && looks)
            {
                --looks;
                Op op;
                Popped const popped = queue.Pop(op);
                if (popped == Popped::None)
                    break;
                if (popped == Popped::Superseded)
                {
                    ++m.superseded;
                    DropAcceptInvite(op);
                    ClearPending(op);
                    queue.Finish(op);
                    continue;
                }
                std::uint32_t const ageMs = nowMs - op.enqueuedMs;
                m.ageMsMax = std::max(m.ageMsMax, ageMs);
                ++m.ageHist[AgeBucket(ageMs)];
                // The transit limit counts from the first look that found a bot in transit, not from the enqueue.
                std::uint32_t const transitMs = op.inTransit ? nowMs - op.transitSinceMs : 0u;
                auto const opStart = Clock::now();
                Verdict const v = Run(op, transitMs, m);
                ++m.verdicts[std::size_t(v)];
                if (v == Verdict::Retry)
                {
                    if (!op.inTransit)
                    {
                        op.inTransit = 1;
                        op.transitSinceMs = nowMs;
                    }
                    later.push_back(op);
                    continue;
                }
                if (v == Verdict::Execute)
                {
                    ++ran;
                    m.opUsMax = std::max(m.opUsMax, UsSince(opStart));
                }
                else
                {
                    ++m.stale[std::size_t(op.kind)];
                    DropAcceptInvite(op);
                }
                // A dropped op: the bot keeps its state and decides again on its next scan (AdhocGroupAction).
                ClearPending(op);
                queue.Finish(op);
            }
            for (Op const& op : later)
                queue.Requeue(op);
            std::uint32_t const wallUs = UsSince(drainStart);
            m.drainUsMax = std::max(m.drainUsMax, wallUs);
            if (ran)
            {
                ++m.ticksWithOp;
                m.stallUsMax = std::max(m.stallUsMax, thread_cpu::StallUs(wallUs, cpuStart, thread_cpu::NowUs()));
            }
        }

        // OB-00 04.11. (a): the oldest op waits longer than AlertAgeSeconds - one line, at most once a minute. The age walks the
        // whole queue (put-back transit ops included), at most once a second.
        if (!w.alertChecked || nowMs - w.lastAlertCheckMs >= kAlertCheckIntervalMs)
        {
            w.alertChecked = true;
            w.lastAlertCheckMs = nowMs;
            std::uint32_t oldestMs = 0;
            bool const hasOldest = queue.OldestEnqueuedMs(nowMs, oldestMs);
            if (AlertDue(hasOldest, oldestMs, sPlayerbotAIConfig.botGroupsOpQueueAlertAgeSeconds, w.alerted, nowMs, w.lastAlertMs))
            {
                w.alerted = true;
                w.lastAlertMs = nowMs;
                sLog.outBasic("[GroupOps] ALERT depth=%u oldest_age_ms=%u ops_per_tick=%u max_depth=%u alert_age_s=%u heavy_skips=%u mode=%u",
                    uint32(queue.Depth()), oldestMs, sPlayerbotAIConfig.botGroupsOpQueueOpsPerTick, sPlayerbotAIConfig.botGroupsOpQueueMaxDepth,
                    sPlayerbotAIConfig.botGroupsOpQueueAlertAgeSeconds, m.heavySkips, sPlayerbotAIConfig.botGroupsOpQueue);
            }
        }
    }

    FlushMinute(w, nowMs);
}

void AddGroupOpsObserver()
{
    new GroupOpsObserver();
}

// ---- A3 lever 2: the write path's producers' side (the bots' own threads) ---------------------------------------------------
bool WritePathRoutes(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.botGroupsWritePath)
        return false;
    // On the world thread (the drain's accept, the managers) the write is where it belongs already.
    if (t_worldThread || t_writeBypass)
        return false;
    return InWritePathScope(ai);
}

bool QueueLeave(PlayerbotAI* ai, OpSource source, std::uint8_t tail)
{
    Player* bot = ai->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    Op op;
    op.kind = OpKind::Leave;
    op.reason = std::uint8_t(kLeaveQueued | tail);   // never the ad-hoc tail (LeaveTailOf)
    op.source = std::uint8_t(source);
    op.bot = bot->GetGUIDLow();
    op.other = group->GetLeaderGuid().GetCounter();
    op.groupId = group->GetId();
    return Enqueue(op, ai, nullptr) == Pushed::Queued;
}

bool QueueAccept(PlayerbotAI* ai, std::uint32_t inviterGuidLow, bool decline)
{
    Op op;
    op.kind = OpKind::Accept;
    op.reason = decline ? kAcceptDecline : 0;
    op.source = std::uint8_t(OpSource::Accept);
    op.bot = ai->GetBot()->GetGUIDLow();
    op.other = inviterGuidLow;
    if (Enqueue(op, ai, nullptr) != Pushed::Queued)
        return false;
    // OB-00 07.11.2026 (c): a queued accept of a real player's invite (ADR-0041 E7 deviation) gets its own count.
    if (!decline && IsRealPlayerOrSelfBot(sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, inviterGuidLow))))
        writeCounts.acceptPlayer.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool QueueInvite(PlayerbotAI* ai, Player* invitee, bool raidConvert)
{
    Player* bot = ai->GetBot();
    Group* group = bot->GetGroup();
    Op op;
    op.kind = OpKind::Invite;
    op.reason = raidConvert ? kInviteRaidConvert : 0;
    op.source = std::uint8_t(OpSource::Invite);
    op.bot = bot->GetGUIDLow();
    op.other = invitee->GetGUIDLow();
    op.groupId = group ? group->GetId() : 0u;
    // The invitee's pending mark only on this map (its AI is touched from this thread; "invite guild"/"invite <name>" can name a
    // bot on another map thread that logs out meanwhile). A real player invitee or another map: no mark - the queue still holds
    // the invitee (dedup through latest), the mark is only the producers' hint.
    PlayerbotAI* inviteeAi = invitee->GetMap() == bot->GetMap() ? GetBotAI(invitee) : nullptr;
    return Enqueue(op, ai, inviteeAi && !inviteeAi->IsRealPlayer() ? inviteeAi : nullptr) == Pushed::Queued;
}

bool QueueLeader(PlayerbotAI* ai, Player* newLeader, bool reset)
{
    Player* bot = ai->GetBot();
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    Op op;
    op.kind = OpKind::Leader;
    op.reason = reset ? kLeaderReset : 0;
    op.source = std::uint8_t(OpSource::Leader);
    op.bot = bot->GetGUIDLow();
    op.other = newLeader->GetGUIDLow();
    op.groupId = group->GetId();
    return Enqueue(op, ai, nullptr) == Pushed::Queued;
}
}
