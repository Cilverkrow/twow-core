// BOT-12 (twow-repo#677, #628; AiPlayerbot.BotGroups.OpQueue): the world-thread group-operation queue and its pure rules
// (GroupOpQueue.h, GroupOpPolicy.h). Both name no game header, so this suite compiles them standalone. What the world thread
// does with an op (GroupOps.cpp) is pinned by t/contracts.d/bot12_group_op_queue_source_contract_tests.cmake.

#include "GroupOpQueue.h"
#include "GroupOpPolicy.h"
#include "GroupAiRefresh.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using namespace ai::group_ops;

Op Leave(std::uint32_t bot, std::uint32_t groupId, std::uint32_t t = 0)
{
    Op op;
    op.kind = OpKind::Leave;
    op.bot = bot;
    op.groupId = groupId;
    op.enqueuedMs = t;
    return op;
}

Op Join(std::uint32_t invitee, std::uint32_t inviter, std::uint32_t groupId = 0, std::uint32_t t = 0)
{
    Op op;
    op.kind = OpKind::Join;
    op.bot = invitee;
    op.other = inviter;
    op.groupId = groupId;
    op.enqueuedMs = t;
    return op;
}
}

int main()
{
    // ---- the switch: CohortPolicy modes, unknown = off ------------------------------------------------------------------
    Require(!QueueApplies(0, 3) && !QueueApplies(0, 4), "mode 0: no bot");
    Require(QueueApplies(1, 3) && QueueApplies(1, 4), "mode 1: every bot");
    Require(QueueApplies(2, 3) && !QueueApplies(2, 4), "mode 2: cohort B (odd low guid) only");
    Require(!QueueApplies(3, 3) && QueueApplies(3, 4), "mode 3: cohort A (even low guid) only");
    Require(!QueueApplies(4, 3) && !QueueApplies(4, 4) && !QueueApplies(0xFFFFFFFFu, 4), "unknown mode: off");
    Require(ValidMode(0) && ValidMode(3) && !ValidMode(4), "valid modes 0..3");
    Require(SanitizeMode(7) == 0 && SanitizeMode(2) == 2, "an unknown mode is sanitised to 0");
    // A join: queued when the mode applies to either bot (OB-00: a queue bot's invites never run on its map thread).
    Require(!JoinQueueApplies(0, 3, 4) && !JoinQueueApplies(9, 3, 4), "join, mode 0 / unknown: map-thread path");
    Require(JoinQueueApplies(1, 3, 4), "join, mode 1: queued");
    Require(JoinQueueApplies(2, 3, 4) && JoinQueueApplies(2, 4, 3), "join, mode 2: a cross-cohort pair is queued, whoever invites");
    Require(JoinQueueApplies(3, 3, 4) && JoinQueueApplies(3, 4, 3), "join, mode 3: a cross-cohort pair is queued, whoever invites");
    Require(!JoinQueueApplies(2, 4, 6) && JoinQueueApplies(2, 5, 7), "join, mode 2: two cohort-A bots keep the old path, two B bots queue");
    Require(!JoinQueueApplies(3, 5, 7) && JoinQueueApplies(3, 4, 6), "join, mode 3: two cohort-B bots keep the old path, two A bots queue");

    // ---- limits ---------------------------------------------------------------------------------------------------------
    Require(ClampOpsPerTick(0) == 1 && ClampOpsPerTick(-5) == 1 && ClampOpsPerTick(1) == 1, "ops per tick: at least 1");
    Require(ClampOpsPerTick(16) == 16 && ClampOpsPerTick(17) == 16 && ClampOpsPerTick(1000) == 16, "ops per tick: at most 16");
    Require(ClampMaxDepth(0) == 16 && ClampMaxDepth(2048) == 2048 && ClampMaxDepth(1 << 20) == 65536, "max depth 16..65536");
    Require(ClampAlertAgeSeconds(0) == 5 && ClampAlertAgeSeconds(60) == 60 && ClampAlertAgeSeconds(99999) == 3600, "alert age 5..3600");

    // ---- the tick gate: OpsPerTick ops, none in a heavy tick -----------------------------------------------------------
    Require(OpsThisTick(1, false) == 1, "default: one op per tick");
    Require(OpsThisTick(1, true) == 0, "a tick with a manager group step runs no op");
    Require(OpsThisTick(16, true) == 0, "the heavy-tick rule holds for any OpsPerTick");
    Require(OpsThisTick(4, false) == 4 && OpsThisTick(0, false) == 1 && OpsThisTick(99, false) == 16, "OpsPerTick clamped");
    Require(LooksThisTick(0) == 0, "a heavy tick looks at nothing");
    Require(LooksThisTick(1) == 1 + kExtraLooksPerTick, "dead entries are cleared in bounded steps");

    // ---- the ALERT gate -------------------------------------------------------------------------------------------------
    Require(!AlertDue(false, 999999, 60, false, 0, 0), "empty queue: no alert");
    Require(!AlertDue(true, 60000, 60, false, 0, 0), "exactly the alert age: no alert");
    Require(AlertDue(true, 60001, 60, false, 1000, 0), "older than the alert age: alert");
    Require(!AlertDue(true, 70000, 60, true, 1000 + kAlertIntervalMs - 1, 1000), "rate limit: one alert per minute");
    Require(AlertDue(true, 70000, 60, true, 1000 + kAlertIntervalMs, 1000), "the next alert after a minute");

    // ---- age_ms_p99: fixed buckets, the percentile as its bucket's upper edge (never above the max) -------------------------
    {
        Require(AgeBucket(0) == 0 && AgeBucket(1) == 0 && AgeBucket(2) == 1 && AgeBucket(500) == 8 && AgeBucket(501) == 9,
            "age buckets by inclusive upper edge (500 is an edge)");
        Require(AgeBucket(60000) == kAgeBuckets - 2 && AgeBucket(60001) == kAgeBuckets - 1 && AgeBucket(0xFFFFFFFFu) == kAgeBuckets - 1,
            "everything above the last edge in the overflow bucket");
        std::uint32_t hist[kAgeBuckets] = {};
        Require(AgeP99Ms(hist, 0) == 0, "no op: 0");
        hist[AgeBucket(40)] = 99;
        hist[AgeBucket(450)] = 1;
        Require(AgeP99Ms(hist, 450) == 50, "100 ops: p99 is the 99th - in the 50 ms bucket");
        hist[AgeBucket(450)] = 2;
        Require(AgeP99Ms(hist, 450) == 450, "101 ops: p99 reaches the top bucket, capped at the max (450, not 500)");
        std::uint32_t one[kAgeBuckets] = {};
        one[AgeBucket(70000)] = 1;
        Require(AgeP99Ms(one, 70000) == 70000, "overflow bucket: the max");
        std::uint32_t edge[kAgeBuckets] = {};
        edge[AgeBucket(500)] = 10;
        Require(AgeP99Ms(edge, 500) == 500 && AgeP99Ms(edge, 499) == 499, "p99 <= 500 is decided exactly");
    }

    // ---- staleness of a leave (design 3.4) ----------------------------------------------------------------------------
    {
        LeaveOpFacts f;
        f.found = true;
        f.inWorld = true;
        f.groupId = 38;
        f.opGroupId = 38;
        Require(DecideLeaveOp(f) == Verdict::Execute, "leave: same group, in the world: execute");
        LeaveOpFacts g = f;
        g.found = false;
        Require(DecideLeaveOp(g) == Verdict::DropLoggedOut, "leave: logged out: drop");
        g = f;
        g.teleporting = true;
        g.transitMs = kTransitMaxAgeMs - 1;
        Require(DecideLeaveOp(g) == Verdict::Retry, "leave: in transit: retry");
        g.transitMs = kTransitMaxAgeMs;
        Require(DecideLeaveOp(g) == Verdict::DropTransit, "leave: in transit too long: drop");
        g = f;
        g.inWorld = false;
        Require(DecideLeaveOp(g) == Verdict::Retry, "leave: not in the world: retry");
        g = f;
        g.realMaster = true;
        Require(DecideLeaveOp(g) == Verdict::DropNotFree, "leave: a real player is its master now: drop");
        g = f;
        g.inBattleground = true;
        Require(DecideLeaveOp(g) == Verdict::DropBg, "leave: in a battleground: drop");
        g = f;
        g.groupId = 0;
        Require(DecideLeaveOp(g) == Verdict::DropAlreadyLeft, "leave: already out of the group: drop");
        g = f;
        g.groupId = 39;
        Require(DecideLeaveOp(g) == Verdict::DropGroupChanged, "leave: in another group now: drop");
        Require(IsDrop(Verdict::DropGroupChanged) && !IsDrop(Verdict::Retry) && !IsDrop(Verdict::Execute), "drop verdicts");
    }

    // ---- staleness of a join -------------------------------------------------------------------------------------------
    {
        JoinOpFacts f;
        f.found = true;
        f.inWorld = true;
        f.sameMap = true;
        f.inRange = true;
        f.maxMembers = 5;
        Require(DecideJoinOp(f) == Verdict::Execute, "join: two ungrouped bots near each other: execute");
        JoinOpFacts g = f;
        g.found = false;
        Require(DecideJoinOp(g) == Verdict::DropLoggedOut, "join: one of them logged out: drop");
        g = f;
        g.teleporting = true;
        Require(DecideJoinOp(g) == Verdict::Retry, "join: in transit: retry");
        g.transitMs = kTransitMaxAgeMs;
        Require(DecideJoinOp(g) == Verdict::DropTransit, "join: in transit too long: drop");
        g = f;
        g.realMaster = true;
        Require(DecideJoinOp(g) == Verdict::DropNotFree, "join: a real-player master: drop");
        g = f;
        g.inBattleground = true;
        Require(DecideJoinOp(g) == Verdict::DropBg, "join: battleground: drop");
        g = f;
        g.inviteeGrouped = true;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: invitee grouped meanwhile: drop");
        g = f;
        g.inviteeInvited = true;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: invitee has another invite: drop");
        g = f;
        g.inviterGroupId = 12;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join into a new group: the inviter grouped meanwhile: drop");
        g = f;
        g.sameMap = false;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: maps differ now: drop");
        g = f;
        g.inRange = false;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: beyond 2 x radius now: drop");

        JoinOpFacts into = f;
        into.opGroupId = 12;
        into.inviterGroupId = 12;
        into.inviterLeads = true;
        into.inviterMembers = 3;
        Require(DecideJoinOp(into) == Verdict::Execute, "join into the inviter's group: execute");
        g = into;
        g.inviterGroupId = 0;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: the inviter's group is gone: drop");
        g = into;
        g.inviterGroupId = 13;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: the inviter is in another group: drop");
        g = into;
        g.inviterLeads = false;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: the inviter no longer leads: drop");
        g = into;
        g.inviterMembers = 5;
        Require(DecideJoinOp(g) == Verdict::DropStalePair, "join: the group is full: drop");
        Require(std::string(VerdictName(Verdict::DropStalePair)) == "drop_stale_pair", "verdict names for the minute line");
    }

    // ---- the queue: FIFO, leaves first, dedup, supersede, full, requeue -----------------------------------------------
    {
        Queue q;
        Op op;
        Require(q.Pop(op) == Popped::None && q.Depth() == 0, "an empty queue pops nothing");

        Op j1 = Join(10, 11, 0, 100);
        Op l1 = Leave(20, 5, 101);
        Op l2 = Leave(21, 6, 102);
        Require(q.Push(j1, 16) == Pushed::Queued && j1.seq != 0, "join queued, seq set");
        Require(q.Push(l1, 16) == Pushed::Queued && q.Push(l2, 16) == Pushed::Queued, "two leaves queued");
        Require(l1.seq != j1.seq && l2.seq != l1.seq, "distinct seqs");
        Require(q.Depth() == 3 && q.IsPending(10) && q.IsPending(11) && q.IsPending(20), "a join holds both bots");

        // Dedup: one op per bot.
        Op dupLeave = Leave(20, 5);
        Require(q.Push(dupLeave, 16) == Pushed::RefusedPending, "a second leave of the same bot is refused");
        Op joinOfLeaver = Join(20, 30);
        Require(q.Push(joinOfLeaver, 16) == Pushed::RefusedPending, "a join for a bot with a pending leave is refused");
        Op joinOfInviter = Join(31, 11);
        Require(q.Push(joinOfInviter, 16) == Pushed::RefusedPending, "an inviter with a pending join takes no second one");
        Require(q.Depth() == 3, "refused ops are not queued");

        // Leaves first, FIFO inside each kind.
        Require(q.Pop(op) == Popped::Live && op.seq == l1.seq, "first the oldest leave");
        q.Finish(op);
        Require(!q.IsPending(20), "a finished op frees its bot");
        Require(q.Pop(op) == Popped::Live && op.seq == l2.seq, "then the next leave");
        q.Finish(op);
        Require(q.Pop(op) == Popped::Live && op.seq == j1.seq, "joins after the leaves");
        Require(q.IsPending(10) && q.IsPending(11), "a popped op stays pending until it is finished");
        q.Finish(op);
        Require(!q.IsPending(10) && !q.IsPending(11) && q.Depth() == 0, "both bots of a join freed");
    }
    {
        // A leave supersedes a pending join of its bot; the join is skipped, its partner freed.
        Queue q;
        Op j = Join(40, 41, 0, 10);
        Require(q.Push(j, 16) == Pushed::Queued, "join queued");
        Op l = Leave(41, 7, 11);
        Require(q.Push(l, 16) == Pushed::Queued, "the inviter's leave replaces its pending join");
        Require(!q.IsPending(40) && q.IsPending(41), "the join's invitee is free again, the leaver pending");
        Op op;
        Require(q.Pop(op) == Popped::Live && op.seq == l.seq, "the leave runs first");
        q.Finish(op);
        Require(q.Pop(op) == Popped::Superseded && op.seq == j.seq, "the superseded join is skipped");
        q.Finish(op);   // harmless: nothing of it is pending any more
        Require(!q.IsPending(40) && !q.IsPending(41) && q.Depth() == 0, "nothing left");
        PushCounts c = q.TakeCounts();
        Require(c.superseding == 1 && c.queued[0] == 1 && c.queued[1] == 1, "counted: one superseding leave, one of each kind");
    }
    {
        // Full: refused and counted per kind; nothing grows beyond MaxDepth (a burst of 1000+ bots after a reset).
        Queue q;
        for (std::uint32_t b = 1; b <= 16; ++b)
        {
            Op o = Leave(b, 100 + b);
            Require(q.Push(o, 16) == Pushed::Queued, "fill up to the depth");
        }
        for (std::uint32_t b = 17; b <= 2000; ++b)
        {
            Op o = (b & 1) ? Leave(b, b) : Join(b, b + 100000);
            Require(q.Push(o, 16) == Pushed::RefusedFull, "a full queue refuses");
        }
        Require(q.Depth() == 16 && !q.IsPending(17) && !q.IsPending(18), "the refused bots are not pending: they decide again");
        PushCounts c = q.TakeCounts();
        Require(c.queued[0] == 16 && c.droppedFull[0] + c.droppedFull[1] == 1984 && c.depthMax == 16, "dropped_full counted");
        Require(q.TakeCounts().depthMax == 16, "depth_max of the next minute starts at the current depth");
    }
    {
        // Transit: the same op back at the end of its FIFO, same seq and enqueue time, still pending; no depth check.
        Queue q;
        Op a = Leave(1, 1, 5);
        Op b = Leave(2, 2, 6);
        q.Push(a, 2);
        q.Push(b, 2);
        Op op;
        Require(q.Pop(op) == Popped::Live && op.seq == a.seq, "pop a");
        q.Requeue(op);
        Require(q.Depth() == 2 && q.IsPending(1), "requeued, still pending, depth back");
        Require(q.Pop(op) == Popped::Live && op.seq == b.seq, "b before the requeued a");
        q.Finish(op);
        Require(q.Pop(op) == Popped::Live && op.seq == a.seq && op.enqueuedMs == 5, "a again, its enqueue time kept");
        q.Finish(op);
    }
    {
        // Transit clock: the drain stamps the first transit look on the op; a requeue keeps it (the 30 s count from there).
        Queue q;
        Op a = Leave(1, 1, 5);
        q.Push(a, 4);
        Op op;
        Require(q.Pop(op) == Popped::Live && !op.inTransit, "not in transit before the first look");
        op.inTransit = 1;
        op.transitSinceMs = 90000;
        q.Requeue(op);
        Require(q.Pop(op) == Popped::Live && op.inTransit && op.transitSinceMs == 90000 && op.enqueuedMs == 5,
            "the transit stamp survives the requeue, the enqueue time too");
        LeaveOpFacts f;
        f.found = true;
        f.teleporting = true;
        f.groupId = 1;
        f.opGroupId = 1;
        f.transitMs = 0;
        Require(DecideLeaveOp(f) == Verdict::Retry, "a backlog op (old enqueue) found in transit at once: it waits, not dropped");
        q.Finish(op);
    }
    {
        // Lock-free depth hint follows every push/pop/requeue/clear.
        Queue q;
        Require(q.DepthHint() == 0, "hint: empty");
        Op a = Leave(1, 1);
        Op b = Join(2, 3);
        q.Push(a, 8);
        q.Push(b, 8);
        Require(q.DepthHint() == 2 && q.Depth() == 2, "hint after two pushes");
        Op op;
        q.Pop(op);
        Require(q.DepthHint() == 1, "hint after a pop");
        q.Requeue(op);
        Require(q.DepthHint() == 2, "hint after a requeue");
        q.Clear();
        Require(q.DepthHint() == 0, "hint after clear");
    }
    {
        // A superseded join keeps its FIFO slot until popped: it counts against MaxDepth (bounded: one dead entry per
        // superseding leave), and the queue stays bounded when filled up.
        Queue q;
        Op j = Join(50, 51);
        Op l = Leave(51, 9);
        Require(q.Push(j, 4) == Pushed::Queued && q.Push(l, 4) == Pushed::Queued, "join, then the inviter's leave");
        Require(q.Depth() == 2, "the dead join still holds a slot");
        Op c = Leave(52, 9);
        Op d = Leave(53, 9);
        Require(q.Push(c, 4) == Pushed::Queued && q.Push(d, 4) == Pushed::Queued, "fill to MaxDepth 4");
        Op e = Leave(54, 9);
        Require(q.Push(e, 4) == Pushed::RefusedFull && q.Depth() == 4, "full with a dead entry: refused, still bounded");
        Op op;
        int live = 0;
        int dead = 0;
        while (true)
        {
            Popped const p = q.Pop(op);
            if (p == Popped::None)
                break;
            (p == Popped::Live ? live : dead) += 1;
            q.Finish(op);
        }
        Require(live == 3 && dead == 1 && q.Depth() == 0, "three live leaves, one dead join, then empty");
        Require(q.Push(e, 4) == Pushed::Queued, "room again after the drain");
    }
    {
        // Oldest age over every entry (a requeued transit op sits at the back with its first enqueue time).
        Queue q;
        Op a = Leave(1, 1, 100);
        Op b = Leave(2, 2, 900);
        q.Push(a, 8);
        q.Push(b, 8);
        Op op;
        q.Pop(op);
        q.Requeue(op);   // a (enqueued at 100) now behind b
        std::uint32_t age = 0;
        Require(q.OldestEnqueuedMs(1000, age) && age == 900, "the requeued op at the back still counts as the oldest");
    }
    {
        // Oldest age; a preset seq (GroupOps.cpp sets it before the push) is kept.
        Queue q;
        std::uint32_t age = 0;
        Require(!q.OldestEnqueuedMs(1000, age), "empty: no oldest");
        Op j = Join(1, 2, 0, 400);
        Op l = Leave(3, 1, 700);
        l.seq = 777;
        q.Push(j, 8);
        q.Push(l, 8);
        Require(l.seq == 777, "a preset seq is kept");
        Require(q.OldestEnqueuedMs(1000, age) && age == 600, "the oldest head counts (the join, 600 ms)");
        Require(q.OldestEnqueuedMs(5, age) && age == 5u - 400u, "differences modulo 2^32 (clock wrap)");
        q.Clear();
        Require(q.Depth() == 0 && !q.IsPending(1) && !q.IsPending(3), "clear");
    }

    // ---- the accept source of the queue: a world-thread source whose tail is always deferred ---------------------------
    {
        using namespace ai::group_ai_refresh;
        Require(std::string(GroupOpSource) == "group op", "the queue's accept source");
        Require(IsWorldManagerSource(GroupOpSource), "the queue's accept is a world-thread accept (no strategy change there)");
        Require(AlwaysDeferTail(GroupOpSource), "the queue's accept tail always goes to the bot's own thread");
        Require(!AlwaysDeferTail("adhoc group") && !AlwaysDeferTail("quest group") && !AlwaysDeferTail(""),
            "the managers keep WorldThreadDefer");
    }

    // ---- off-thread attribution by the outermost bot action -----------------------------------------------------------
    Require(OffSourceOf(false, "") == OffSource::Other, "no action: other");
    Require(OffSourceOf(true, "ad-hoc group") == OffSource::Adhoc, "ad-hoc action");
    Require(OffSourceOf(true, "leave far away") == OffSource::LeaveFar, "leave far away");
    Require(OffSourceOf(true, "repop") == OffSource::Repop && OffSourceOf(true, "auto release") == OffSource::Repop &&
        OffSourceOf(true, "release") == OffSource::Repop, "repop / release spirit");
    Require(OffSourceOf(true, "accept invitation") == OffSource::Accept, "accept invitation");
    Require(OffSourceOf(true, "accept invitation", true) == OffSource::Adhoc,
        "the accept of an ad-hoc invite (the old path's group create) counts as the ad-hoc path");
    Require(OffSourceOf(true, "accept invitation", false) == OffSource::Accept && OffSourceOf(true, "leave far away", true) == OffSource::LeaveFar,
        "the ad-hoc tag changes only the accept");
    Require(OffSourceOf(true, "invite nearby") == OffSource::InviteLfg && OffSourceOf(true, "lfg") == OffSource::InviteLfg &&
        OffSourceOf(true, "lfg join") == OffSource::InviteLfg && OffSourceOf(true, "join") == OffSource::InviteLfg, "invite / LFG");
    Require(OffSourceOf(true, "leave") == OffSource::Command && OffSourceOf(true, "uninvite") == OffSource::Command, "commands");
    Require(OffSourceOf(true, "dungeon clear advance") == OffSource::DungeonClear && OffSourceOf(true, "dc on") == OffSource::DungeonClear,
        "dungeon clear");
    Require(OffSourceOf(true, "follow") == OffSource::OtherAction, "any other action");
    Require(std::string(OffSourceName(OffSource::LeaveFar)) == "leave_far", "source names for the minute line");
    return 0;
}
