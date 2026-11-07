#pragma once

#include <cstdint>

class Player;

// L1 (owner 07.11.2026, AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly): the game side of LockoutPolicy.h - the split
// guard at the accept (S1) and at the module's own moves into a raid (S3), and the counters of the [Lockout] minute line.
//
// Thread rule: the guard reads only the bot's own binds (Player::GetBoundInstance on the bot) and scalar ids of the other
// player (GetMapId, GetInstanceId). It never reads Group::m_boundInstances or another player's binds: those std::maps are
// written by other map threads.
namespace ai::lockout_guard
{
enum Counter
{
    ResetReal = 0,
    ResetBot,
    KeepBot,
    SplitDecline,
    SplitEntry,
    kCounters,
};

// Relaxed increment, any thread.
void Count(Counter c);

// The accept tail's outcome: reset with a real inviter, reset with a bot inviter (switch 0), or binds kept.
void CountJoin(bool reset, bool inviterReal);

// True when the bot holds a PERM bind on dungeon map mapId for another copy than targetInstanceId (LockSplit).
bool SplitToward(Player* bot, std::uint32_t mapId, std::uint32_t targetInstanceId);

// S3 for an area-trigger relay: the trigger's teleport goes to map M and target stands on M in another copy of a raid the
// bot is perm-bound to. No target, a target not on M or a trigger without a teleport = no check (false).
bool RelaySplit(Player* bot, std::uint32_t triggerId, Player* target);

// World thread (PlayerbotWorldScript::OnUpdate): once a minute, the [Lockout] line when a counter moved.
void MinuteTick(std::uint32_t diff);
}
