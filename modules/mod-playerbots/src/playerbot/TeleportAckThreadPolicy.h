#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ai::teleport_ack
{
// twow-repo#563 X5 (CLI-ROT 02.11.2026, OB-00; AiPlayerbot.ThreadSafeTeleportAck, default 0). PlayerbotHolder::UpdateSessions
// acks a held bot's pending teleport (HandleTeleportAck -> StopMoving -> MotionMaster::DirectClean, near: HandleMoveTeleportAck).
// For a master's holder (PlayerbotMgr) that runs inside the MASTER's Map::Update - on the master's map thread - while the bot
// may be in world on another map that a different thread updates at the same time. Crash of 02.11.2026 15:19:28Z (track B,
// b43284e9): the DC test driver parked on map 0, its bot near-teleporting in BFD instance 107, MotionMaster::DirectClean ran a
// stale PointMovementGenerator<Player> (RIP on its typeinfo). With the switch a master's holder hands a bot that is in world and
// near-teleporting to the bot's own update (PlayerbotAI::UpdateAI on the bot's map thread).
//
// twow-repo#676 (BOT-HOLDER-FARACK-01; CLI-ROT 05.11.2026, OB-00 / OB-10; no switch, always on): a FAR teleport of a master
// holder's bot is no longer acked by the holder either. HandleTeleportAck -> HandleMoveWorldportAckOpcode -> Map::Add puts the
// bot into the destination map, whose worker updates it from then on, and the holder thread then went on with Reset() ->
// StopMoving() -> MotionMaster Clear + MoveIdle and the AI value resets: cross-thread. The holder now only queues the bot's guid
// (FarAcks); PlayerbotHolder::DrainFarTeleportAcks runs the whole HandleTeleportAck from PlayerbotWorldScript::OnUpdate, after
// sMapMgr.Update returned (all maps idle). The random bot holder runs in the world-thread phase already: unchanged.
// Pure rules, no game types.
enum class Acker
{
    Holder,      // UpdateSessions acks it (as before)
    BotThread,   // UpdateSessions only requests it, the bot's own UpdateAI acks it (X5, near)
    WorldThread  // UpdateSessions only queues the guid, the world-thread drain acks it (#676, far)
};

// on: the X5 switch; masterHolder: the holder is a master's PlayerbotMgr; inWorld / nearTeleport / farTeleport: the bot as the
// holder sees it in this pass. A far teleport of a master holder's bot goes to the world thread whatever the switch, and also
// when it is still seen in world: Player::TeleportTo removes the player from the old map before it sets the far semaphore, so
// "far in world" is only another thread's TeleportTo in progress - the drain checks again with the maps idle.
inline Acker Choose(bool on, bool masterHolder, bool inWorld, bool nearTeleport, bool farTeleport)
{
    if (!masterHolder)
        return Acker::Holder;
    if (farTeleport)
        return Acker::WorldThread;
    if (on && inWorld && nearTeleport)
        return Acker::BotThread;
    return Acker::Holder;
}

// The bot's own update acks only what a holder requested, and only while the near teleport is still pending in world.
inline bool BotThreadAcks(bool requested, bool inWorld, bool nearTeleport)
{
    return requested && inWorld && nearTeleport;
}

// The world-thread drain acks a queued guid only when the bot was found with its AI, is out of world and its far teleport is
// still pending. Otherwise it was acked, cancelled or logged out since (or is still in world: the next holder pass queues it
// again).
inline bool WorldThreadAcks(bool foundWithAi, bool inWorld, bool farTeleport)
{
    return foundWithAi && !inWorld && farTeleport;
}

// The guids a master's holder handed to the world thread. Producers: the masters' map threads (UpdateSessions); consumer: the
// world thread (DrainFarTeleportAcks). A guid is queued once until the drain takes it; at maxDepth a push is refused (the next
// holder pass tries again). Guids, no pointers: the drain resolves the bot again.
constexpr std::size_t kFarAckMaxDepth = 1024;

enum class Queued
{
    Added,
    Duplicate,
    Full
};

class FarAckQueue
{
public:
    Queued Push(std::uint32_t guidLow, std::size_t maxDepth = kFarAckMaxDepth)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (std::find(guids.begin(), guids.end(), guidLow) != guids.end())
            return Queued::Duplicate;
        if (guids.size() >= maxDepth)
            return Queued::Full;
        guids.push_back(guidLow);
        depth.store(guids.size(), std::memory_order_release);
        return Queued::Added;
    }

    // All queued guids in push order; the queue is empty afterwards.
    std::vector<std::uint32_t> Take()
    {
        std::vector<std::uint32_t> out;
        std::lock_guard<std::mutex> lock(mutex);
        out.swap(guids);
        depth.store(0, std::memory_order_release);
        return out;
    }

    // Lock-free fast path for the drain (every world tick).
    bool Empty() const { return depth.load(std::memory_order_acquire) == 0; }

private:
    std::mutex mutex;
    std::vector<std::uint32_t> guids;
    std::atomic<std::size_t> depth{0};
};

inline FarAckQueue& FarAcks()
{
    static FarAckQueue queue;
    return queue;
}

// Per-minute counts for the [WorldBots] line, exchanged to 0 when the line is written - increments.
// X5 suffix (only with the switch on): requested by a master's holder, acked on the bot's thread; waits stays in that suffix for
// its readers and is 0 since #676 (a far teleport is no longer held back, it goes to the world thread).
// #676 suffix (always): far acks queued by a master's holder, acked by the world-thread drain, skipped by it (WorldThreadAcks
// false), refused at the depth limit.
struct Counters
{
    std::atomic<std::uint32_t> requested{0};
    std::atomic<std::uint32_t> botAcks{0};
    std::atomic<std::uint32_t> waits{0};
    std::atomic<std::uint32_t> farQueued{0};
    std::atomic<std::uint32_t> farWorldAcks{0};
    std::atomic<std::uint32_t> farSkips{0};
    std::atomic<std::uint32_t> farFull{0};
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}
}
