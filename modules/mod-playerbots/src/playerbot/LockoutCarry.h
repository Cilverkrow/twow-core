#pragma once

#include <cstdint>

class Player;

// leader_bind_carry (OB-00 07.11.2026, AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly section): count only, no rule (the
// rule comes with the raid ADR, O7), at both switch values. The definition is LockoutPolicy.h (CarryEntry, LeaderBindCarry);
// the fields are leader_carry_real / leader_carry_bot at the end of the [Lockout] minute line (LockoutGuard.cpp).
//
// Hooks (PlayerbotScripts.cpp, PlayerbotPlayerScript), all three read and count only:
//   - OnBeforeTeleport (the core's teleport in Player.cpp, on the caller's thread): a far teleport into a dungeon map notes
//     the player's own bind on that map. Its own data, read where the core reads the same binds a few lines later (the far
//     branch, GetBoundInstanceSaveForSelfOrGroup).
//   - OnMapChanged (the end of WorldSession::HandleMoveWorldportAckOpcode, after Map::Add and so after the core's bind on
//     entry): takes the note and decides. The member's own binds are read first. The group's bind and the leader's own bind
//     are read only on the world thread (World::IsWorldThread): far acks there run with every map idle - real players in
//     World::UpdateSessions before sMapMgr.Update, bots in WORLDHOOK_ON_UPDATE after it (#676 drain, random bot holder).
//     An ack on any other thread is not counted.
//   - OnLogout: drops the note.
// Not counted: an entry without a note (login inside the map, no far teleport before the ack), an ack off the world thread,
// a leader that is not online.
namespace ai::lockout_carry
{
void NoteTeleport(Player* player, std::uint32_t mapId);
void OnEntry(Player* player);
void Forget(Player* player);
}
