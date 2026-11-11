#pragma once

#include <cstdint>

// A3 step 6 T30-1 (design T30 v1.1 2.1, 3.6, 4; AiPlayerbot.BotGroups.Manager.Types, default 0): what the bots' map threads
// may ask the group manager. Light on purpose (no game type): the gates G1-G12, the S7 mirror hooks and the status post include
// this alone. Definitions in GroupManager.cpp. With Types 0 every answer is false / 0 without a lock: OwnsBot reads the two
// switches, the registry answers from an atomic that stays false while it holds nobody.
namespace ai::group_mgr
{
// F1 (a): the bot is in the manager's cohort (BotGroups.Manager) and a type is on (Types != 0, effective after F9).
bool OwnsBot(std::uint32_t guidLow);
// The registry holds the bot: a member, an Exit still open, an exit block, or a group it could not confirm leaving.
bool IsManaged(std::uint32_t guidLow);
// The supervisor's GROUP handler (T02-GROUP ManagedNow; at most once per 10 s per bot, in GROUP or in RESCUE with a held
// group): the registry holds the bot as a live member of the group whose serial has these low 16 bits (the directive's b16-31).
// False for kOrphanGraceMs (GroupManagerPolicy.h) = the bot leaves GROUP. Same fast path as above; no log, no allocation, the
// lock only for the lookup.
bool IsManaged(std::uint32_t guidLow, std::uint16_t serial);
// The one gate of the legacy group writers (G1-G12): owned or held. False for every bot while Types is 0 and nothing is held.
bool Blocks(std::uint32_t guidLow);
// The S7 role mirror (3.6) for a member of a manager QUEST group: 0 none, 1 follower, 2 leader.
std::uint8_t QuestRoleOf(std::uint32_t guidLow);
bool IsQuestMember(std::uint32_t guidLow);
bool IsQuestFollower(std::uint32_t guidLow);
// The S7 queue age mirror (3.6): seconds the bot waits on the quest in the manager's queue (0 = not queued).
std::uint32_t QueuedAge(std::uint32_t guidLow, std::uint32_t questId);
// The key of a manager group by its core group id (ManagerFacts::ReadKey): false = not a manager group.
bool ManagedKeyOf(std::uint32_t coreGroupId, std::uint32_t& questId, std::int8_t& objective);
}
