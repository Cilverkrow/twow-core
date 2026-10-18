#pragma once

#include <cstdint>
#include <string>

// twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace, default 0): runtime of the cast diagnostics. Callers check
// the switch first; nothing here reads the config. Rules and line formats: RotCastPolicy.h.
// Never included from PlayerbotAI.h or PlayerbotAIConfig.h (botpch chain).
namespace ai::rot_cast_rt
{
// Interned action name for the counter key (1..1022; 1023 = "other" once the table is full). Thread-safe; callers
// keep the slot in the action, so this runs once per action instance.
std::uint16_t ActionSlot(std::string const& action);

// Map thread (or World thread): counts one outcome in this thread's table.
void RecordEval(std::uint8_t cls, bool cohortB, std::uint16_t slot, std::uint16_t code);
void RecordStart(std::uint8_t cls, bool cohortB, std::uint16_t slot, std::uint16_t code);
void RecordServer(std::uint8_t cls, bool cohortB, std::uint32_t spellId, std::uint16_t code);

// Map thread, once per bot update: hands this thread's table to the minute book after the World thread closed a
// window, so a quiet thread's counts are not held back.
void FlushIfDue();

// Line budgets per minute; false = no line (counted as *_dropped in the [RotCast] summary).
bool TakeUnknownBudget(std::uint32_t now);
bool TakePullBudget(std::uint32_t now);

// [RotRole]: true when the bot's signature changed and the budget allows a line. `suppressed` = resets with an
// unchanged signature since the bot's last line.
bool RoleLineDue(std::uint32_t guid, std::uint64_t signature, std::uint32_t now, std::uint32_t& suppressed);

// World thread only (RandomPlayerbotMgr::UpdateAIInternal). firstCall: the switch just went on - clear and start a
// window. Otherwise once per minute [RotCast] (+ top rows), once per hour [RotCastTotal] (+ top rows).
void Report(bool firstCall, std::uint32_t now);
}
