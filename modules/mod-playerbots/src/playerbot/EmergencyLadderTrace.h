#pragma once

#include <cstdint>

#include "playerbot/EmergencyLadderPolicy.h"

// twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): counters and line budget of the emergency ladder. Callers check the
// switch first; nothing here reads the config. Never included from PlayerbotAI.h or PlayerbotAIConfig.h (botpch chain).
namespace ai::emergency_ladder_rt
{
// Bot thread. Episodes are rare (a solo bot below critical health), so one short std::mutex per event is enough.
void RecordEpisode(std::uint8_t cls, bool cohortB, ai::emergency_ladder::Choice choice);
void RecordEscapeFallback(std::uint8_t cls, bool cohortB);   // the escape window ran out and the bot fled after all
void RecordFightAbort(std::uint8_t cls, bool cohortB);       // a fight episode turned into a flee (second mob, target healed)
void RecordSoloCc(std::uint8_t cls, bool cohortB);           // a solo CC target was picked (new target)
void RecordCcBroken(std::uint8_t cls, bool cohortB);         // the bot's own CC on an add ended within CcBrokenBeforeMs

// One [EmergencyLadder] line: at most 300 a minute over all bots; false = no line (counted as dropped=).
bool TakeLineBudget(std::uint32_t now);

// World thread only. firstCall: the switch just went on - clear and start a window. Otherwise once per hour one
// [EmergencyLadderTotal] line per cohort and class with counts.
void Report(bool firstCall, std::uint32_t now);
}
