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
// World::UpdateSessions, where the core handles the players' group opcodes (PACKET_PROCESS_GROUP = PACKET_PROCESS_WORLD):
// a group event on it happens with the maps idle (outside sMapMgr.Update). Any other thread is a map/region thread, a
// command thread or another module's thread - "off the world thread".
thread_local bool t_worldThread = false;
std::atomic<bool> worldSeen{ false };

enum GroupEvent : std::size_t
{
    EvCreate = 0,
    EvDisband,
    EvAdd,
    EvRemove,
    kEvents,
};

struct OffCounts
{
    std::array<std::atomic<std::uint32_t>, kEvents> off{};
    std::array<std::atomic<std::uint32_t>, kOffSources> bySource{};   // creates + disbands
    std::atomic<std::uint32_t> queueBot{ 0 };     // creates + disbands from a bot OpQueue applies to
    std::atomic<std::uint32_t> realMaster{ 0 };   // creates + disbands from a bot with a real-player master (PR3)
    std::atomic<std::uint32_t> wtCreate{ 0 };
    std::atomic<std::uint32_t> wtDisband{ 0 };
};

OffCounts offCounts;

bool ObserverOn()
{
    return sPlayerbotAIConfig.botGroupsDiagnostics || sPlayerbotAIConfig.botGroupsOpQueue;
}

// Counter only: relaxed atomics, no lock, no log line per event.
void Observe(GroupEvent e)
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
    if (!mapWrite)
        return;
    OffSource const src = t_outerAction ? OffSourceOf(true, t_outerAction->getName()) : OffSource::Other;
    offCounts.bySource[std::size_t(src)].fetch_add(1, std::memory_order_relaxed);
    if (t_actionAi)
    {
        if (Player* bot = t_actionAi->GetBot())
            if (QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, bot->GetGUIDLow()))
                offCounts.queueBot.fetch_add(1, std::memory_order_relaxed);
        if (t_actionAi->HasRealPlayerMaster())
            offCounts.realMaster.fetch_add(1, std::memory_order_relaxed);
    }
}

// The core's group hooks (Group::Create, Group::Disband, AddMember, RemoveMember; src/game/Group/Group.cpp) fire on the
// thread that changes the group. Create/Disband of a non-battleground group are the m_GroupMap writes (AddGroup after
// Create in HandleGroupAcceptOpcode, RemoveGroup after the disband in Player::RemoveFromGroup / UninviteFromGroup).
// No core hook was needed.
class GroupOpsObserver : public GroupScript
{
public:
    GroupOpsObserver() : GroupScript("PlayerbotGroupOpsObserver") {}

    void OnCreate(Group* group, ObjectGuid /*leaderGuid*/, uint8 /*groupType*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvCreate);
    }
    void OnAddMember(Group* group, ObjectGuid /*guid*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvAdd);
    }
    void OnRemoveMember(Group* group, ObjectGuid /*guid*/, uint8 /*method*/) override
    {
        if (group && !group->isBGGroup())
            Observe(EvRemove);
    }
    void OnDisband(Group* group) override
    {
        if (group && !group->isBGGroup())
            Observe(EvDisband);
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
    if (op.kind == OpKind::Join)
        ClearPending(op.other, op.seq);
}

// The core leave of the ad-hoc manager (WorldThreadManager::LeaveGroup, S7 LeaveCore): HandleGroupDisbandOpcode ->
// RemoveFromGroup -> RemoveMember, or Disband + RemoveGroup + delete for a group of two. With the maps idle. The log lines and
// the AI part (strategy reset) go to the bot's own thread - always, whatever WorldThreadDefer says.
Verdict RunLeave(Op const& op, std::uint32_t ageMs, DrainMinute& m)
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
    f.ageMs = ageMs;
    Verdict const v = DecideLeaveOp(f);
    if (v != Verdict::Execute)
        return v;

    Group* group = bot->GetGroup();
    // The ad-hoc registry: the group ends with the second-to-last member (core disband).
    if (group->GetMembersCount() <= 2)
        adhoc_group::Groups().Forget(group->GetId());
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

    if (sRandomPlayerbotMgr.IsFreeBot(bot))
        ai->SetMaster(nullptr);
    ai->RequestGroupAi(group_ai_refresh::LeaveReset);

    Group* after = bot->GetGroup();
    if (after && after->GetId() == op.groupId)
        ++m.failed[std::size_t(OpKind::Leave)];
    else
        ++m.executed[std::size_t(OpKind::Leave)];
    return v;
}

// The core join of the ad-hoc manager (WorldThreadManager::Join): the inviter's invite opcode, then the invitee's
// "accept invitation" with the group-op source - the accept keeps only the core join here and posts its tail to the bot
// (AcceptInvitationAction, group_ai_refresh::GroupOpSource) - then the ad-hoc registry and free-for-all loot.
Verdict RunJoin(Op const& op, std::uint32_t ageMs, DrainMinute& m)
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
    f.ageMs = ageMs;
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
    for (std::size_t i = 0; i < kOffSources; ++i)
        bySource[i] = Take(offCounts.bySource[i]);
    std::uint32_t const queueBot = Take(offCounts.queueBot);
    std::uint32_t const realMaster = Take(offCounts.realMaster);
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
    AddField(line, "off_map_queue_bot", queueBot);
    AddField(line, "off_map_real_master", realMaster);
    for (std::size_t i = 0; i < kOffSources; ++i)
        AddField(line, ("off_map_" + std::string(OffSourceName(OffSource(i)))).c_str(), bySource[i]);
    AddField(line, "wt_create", wtCreate);
    AddField(line, "wt_disband", wtDisband);
    sLog.outBasic("%s", line.c_str());
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
    if (!ai->GroupOpPending())
        return false;
    if (Ops().IsPending(guidLow))
        return true;
    ai->ClearGroupOpPendingAny();   // ran or dropped while the bot was gone: the queue is the truth
    return false;
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
    std::uint32_t const nowMs = NowMs();
    Queue& queue = Ops();

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
                    ClearPending(op);
                    queue.Finish(op);
                    continue;
                }
                std::uint32_t const ageMs = nowMs - op.enqueuedMs;
                m.ageMsMax = std::max(m.ageMsMax, ageMs);
                auto const opStart = Clock::now();
                Verdict const v = op.kind == OpKind::Leave ? RunLeave(op, ageMs, m) : RunJoin(op, ageMs, m);
                ++m.verdicts[std::size_t(v)];
                if (v == Verdict::Retry)
                {
                    later.push_back(op);
                    continue;
                }
                if (v == Verdict::Execute)
                {
                    ++ran;
                    m.opUsMax = std::max(m.opUsMax, UsSince(opStart));
                }
                else
                    ++m.stale[std::size_t(op.kind)];
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

        // OB-00 04.11. (a): the oldest op waits longer than AlertAgeSeconds - one line, at most once a minute.
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

    FlushMinute(w, nowMs);
}

void AddGroupOpsObserver()
{
    new GroupOpsObserver();
}
}
