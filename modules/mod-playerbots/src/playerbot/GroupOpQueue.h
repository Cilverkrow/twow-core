#pragma once

#include "GroupOpPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

// BOT-12 (twow-repo#677, AiPlayerbot.BotGroups.OpQueue): the world-thread group-operation queue. Producers (the bots' map
// threads) push plain ops - guids and ids, no pointer, no closure (the WorldThreadCommandQueue.h rule); the world thread pops
// them in PlayerbotWorldScript::OnUpdate with the maps idle and resolves every guid again (GroupOps.cpp). Names no game
// header, so t/group_op_queue_tests.cpp compiles it standalone. One mutex, held only to push, pop or read a count.
namespace ai::group_ops
{
struct Op
{
    OpKind kind = OpKind::Leave;
    std::uint8_t reason = 0;        // Leave: the ad-hoc reason (adhoc_group::Leave); Accept/Invite/Leader: kAcceptDecline etc.
    std::uint8_t source = 0;        // OpSource
    std::uint8_t objective = 0;     // the ad-hoc key, registered on a join
    std::uint8_t inTransit = 0;     // set by the drain at the first look that found a bot in transit (transitSinceMs valid)
    std::uint32_t bot = 0;          // the bot whose group changes: the leaver, the invitee; Invite: the inviter; Leader: the leader
    std::uint32_t other = 0;        // Leave: the group leader at the decision; Join: the inviter; Accept: the invite's leader;
                                    // Invite: the invitee; Leader: the new leader
    std::uint32_t groupId = 0;      // Leave: the group it decided to leave; Join: the joining group (0 = a new one); Invite: the
                                    // inviter's group (0 = none); Leader: the group; Accept: 0
    std::uint32_t questId = 0;
    std::uint32_t enqueuedMs = 0;
    std::uint32_t seq = 0;          // never 0 once queued (GroupOps.cpp sets it before the push, else Push does)
    std::uint32_t transitSinceMs = 0;   // the drain's clock at that first transit look (the 30 s transit limit counts from here)
};

enum class Pushed : std::uint8_t
{
    Queued = 0,
    RefusedFull,       // depth at OpQueueMaxDepth: dropped_full - the producer keeps its state and decides again later
    RefusedPending,    // one of its bots already has an op queued (a duplicate)
};

enum class Popped : std::uint8_t
{
    None = 0,          // empty
    Live,              // the op to check and run
    Superseded,        // a later op of its bot replaced it: clear its pending flags, do not run it
};

// Producer-side counts (under the mutex), taken once per minute.
struct PushCounts
{
    std::array<std::uint32_t, kKinds> queued{};
    std::array<std::uint32_t, kKinds> droppedFull{};
    std::array<std::uint32_t, kKinds> refusedPending{};
    std::uint32_t superseding = 0;  // a Leave that replaced a pending Join (A3 lever 2: or Accept or Invite)
    std::uint32_t depthMax = 0;
};

class Queue
{
public:
    // Dedup (design 3.4): one op per bot. A Leave replaces a pending Join of its bot (the join's entry stays in the FIFO and is
    // skipped as superseded); every other op of a bot that already has one is refused. A Join holds both bots (inviter and
    // invitee). Full: refused, nothing queued.
    // A3 lever 2 (BotGroups.WritePath): a Leave replaces a pending Join, Accept or Invite of its bot (LeaveSupersedes); an
    // Invite holds both bots like a Join (HoldsBoth); Accept and Leader hold their op.bot only.
    Pushed Push(Op& op, std::size_t maxDepth)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::size_t const k = std::size_t(op.kind);
        if (depth >= maxDepth)
        {
            ++counts.droppedFull[k];
            return Pushed::RefusedFull;
        }

        auto const mine = latest.find(op.bot);
        bool supersedes = false;
        if (op.kind == OpKind::Leave)
        {
            if (mine != latest.end())
            {
                if (!LeaveSupersedes(mine->second.kind))
                {
                    ++counts.refusedPending[k];
                    return Pushed::RefusedPending;
                }
                supersedes = true;
            }
        }
        else if (mine != latest.end() || (HoldsBoth(op.kind) && latest.count(op.other)))
        {
            ++counts.refusedPending[k];
            return Pushed::RefusedPending;
        }

        if (!op.seq)   // GroupOps.cpp sets it before the push (the pending marks first); the tests leave it to the queue
        {
            op.seq = nextSeq++;
            if (!nextSeq)
                nextSeq = 1;
        }
        if (supersedes)
        {
            // The join's partner is free again; the join itself is skipped when it comes up.
            Pending const old = mine->second;
            if (old.partner)
            {
                auto const partner = latest.find(old.partner);
                if (partner != latest.end() && partner->second.seq == old.seq)
                    latest.erase(partner);
            }
            ++counts.superseding;
        }
        latest[op.bot] = { op.seq, op.kind, HoldsBoth(op.kind) ? op.other : 0u };
        if (HoldsBoth(op.kind))
            latest[op.other] = { op.seq, op.kind, op.bot };
        fifo[k].push_back(op);
        ++depth;
        depthHint.store(depth, std::memory_order_relaxed);
        ++counts.queued[k];
        if (depth > counts.depthMax)
            counts.depthMax = std::uint32_t(depth);
        return Pushed::Queued;
    }

    // Leaves first (releases before formations, as S7's StepOne). The op leaves the queue; its bots stay pending until Finish
    // (or a Requeue puts it back). A3 lever 2: kPopOrder - Leave, Leader, Accept, Invite, Join.
    Popped Pop(Op& out)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<Op>* q = nullptr;
        for (OpKind kind : kPopOrder)
            if (!fifo[std::size_t(kind)].empty())
            {
                q = &fifo[std::size_t(kind)];
                break;
            }
        if (!q)
            return Popped::None;
        out = q->front();
        q->pop_front();
        --depth;
        depthHint.store(depth, std::memory_order_relaxed);
        return IsLatestLocked(out) ? Popped::Live : Popped::Superseded;
    }

    // A bot in transit: the same op (same seq, same enqueue time) at the back of its FIFO. It held a slot already, so the depth
    // limit does not apply.
    void Requeue(Op const& op)
    {
        std::lock_guard<std::mutex> lock(mutex);
        fifo[std::size_t(op.kind)].push_back(op);
        ++depth;
        depthHint.store(depth, std::memory_order_relaxed);
    }

    // The op ran or was dropped: its bots may queue again.
    void Finish(Op const& op)
    {
        std::lock_guard<std::mutex> lock(mutex);
        EraseIfLatest(op.bot, op.seq);
        if (HoldsBoth(op.kind))
            EraseIfLatest(op.other, op.seq);
    }

    bool IsPending(std::uint32_t bot) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return latest.count(bot) != 0;
    }

    std::size_t Depth() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return depth;
    }

    // Lock-free: the depth as of the last push/pop (the drain's empty-queue fast path; exact for the world thread, which is
    // the only popper, up to pushes racing it - those wait one tick).
    std::size_t DepthHint() const
    {
        return depthHint.load(std::memory_order_relaxed);
    }

    // The age of the oldest entry: every entry, since an op put back for transit sits at the back of its FIFO with its
    // first enqueue time (O(depth) - the drain calls it at most once per kAlertCheckIntervalMs).
    bool OldestEnqueuedMs(std::uint32_t nowMs, std::uint32_t& ageMs) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool any = false;
        ageMs = 0;
        for (auto const& q : fifo)
            for (Op const& op : q)
            {
                std::uint32_t const age = nowMs - op.enqueuedMs;
                ageMs = any ? (age > ageMs ? age : ageMs) : age;
                any = true;
            }
        return any;
    }

    PushCounts TakeCounts()
    {
        std::lock_guard<std::mutex> lock(mutex);
        PushCounts out = counts;
        counts = PushCounts();
        counts.depthMax = std::uint32_t(depth);
        return out;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& q : fifo)
            q.clear();
        latest.clear();
        depth = 0;
        depthHint.store(0, std::memory_order_relaxed);
    }

private:
    struct Pending
    {
        std::uint32_t seq = 0;
        OpKind kind = OpKind::Leave;
        std::uint32_t partner = 0;   // Join, Invite: the other bot
    };

    bool IsLatestLocked(Op const& op) const
    {
        auto const a = latest.find(op.bot);
        if (a == latest.end() || a->second.seq != op.seq)
            return false;
        if (!HoldsBoth(op.kind))
            return true;
        auto const b = latest.find(op.other);
        return b != latest.end() && b->second.seq == op.seq;
    }

    void EraseIfLatest(std::uint32_t bot, std::uint32_t seq)
    {
        auto const it = latest.find(bot);
        if (it != latest.end() && it->second.seq == seq)
            latest.erase(it);
    }

    mutable std::mutex mutex;
    std::array<std::deque<Op>, kKinds> fifo;   // [Leave], [Join], [Accept], [Invite], [Leader] (popped in kPopOrder)
    std::unordered_map<std::uint32_t, Pending> latest;
    std::size_t depth = 0;
    std::atomic<std::size_t> depthHint{ 0 };   // a copy of depth, written under the mutex, read without it
    std::uint32_t nextSeq = 1;
    PushCounts counts;
};
}
