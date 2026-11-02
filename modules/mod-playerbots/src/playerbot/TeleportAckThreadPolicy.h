#pragma once

#include <atomic>
#include <cstdint>

namespace ai::teleport_ack
{
// twow-repo#563 X5 (CLI-ROT 02.11.2026, OB-00; AiPlayerbot.ThreadSafeTeleportAck, default 0). PlayerbotHolder::UpdateSessions
// acks a held bot's pending teleport (HandleTeleportAck -> StopMoving -> MotionMaster::DirectClean, near: HandleMoveTeleportAck).
// For a master's holder (PlayerbotMgr) that runs inside the MASTER's Map::Update - on the master's map thread - while the bot
// may be in world on another map that a different thread updates at the same time. Crash of 02.11.2026 15:19:28Z (track B,
// b43284e9): the DC test driver parked on map 0, its bot near-teleporting in BFD instance 107, MotionMaster::DirectClean ran a
// stale PointMovementGenerator<Player> (RIP on its typeinfo). With the switch a master's holder hands a bot that is in world and
// near-teleporting to the bot's own update (PlayerbotAI::UpdateAI on the bot's map thread); a far teleport has already left the
// old map (Player::TeleportTo removes the player before the far semaphore is set), so the holder keeps it - if a far-teleporting
// bot is still seen in world, nobody acks it this pass. The random bot holder runs in the world-thread phase, not in parallel
// with the maps: unchanged. Pure rules, no game types.
enum class Acker
{
    Holder,     // UpdateSessions acks it (as before)
    BotThread,  // UpdateSessions only requests it, the bot's own UpdateAI acks it
    Wait        // nobody acks it this pass
};

// on: the switch; masterHolder: the holder is a master's PlayerbotMgr; inWorld / nearTeleport / farTeleport: the bot as the holder
// sees it in this pass.
inline Acker Choose(bool on, bool masterHolder, bool inWorld, bool nearTeleport, bool farTeleport)
{
    if (!on || !masterHolder || !inWorld)
        return Acker::Holder;
    if (nearTeleport)
        return Acker::BotThread;
    if (farTeleport)
        return Acker::Wait;
    return Acker::Holder;
}

// The bot's own update acks only what a holder requested, and only while the near teleport is still pending in world.
inline bool BotThreadAcks(bool requested, bool inWorld, bool nearTeleport)
{
    return requested && inWorld && nearTeleport;
}

// Per-minute counts for the [WorldBots] line (only with the switch on): requested by a master's holder, acked on the bot's thread,
// held back (far in world). Exchanged to 0 when the line is written - increments.
struct Counters
{
    std::atomic<std::uint32_t> requested{0};
    std::atomic<std::uint32_t> botAcks{0};
    std::atomic<std::uint32_t> waits{0};
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}
}
