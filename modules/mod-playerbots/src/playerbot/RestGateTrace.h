#pragma once

#include <cstdint>

#include "playerbot/RestGatePolicy.h"

// twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): counters and line budget of the rest gate. Callers check the switch first;
// nothing here reads the config. Never included from PlayerbotAI.h or PlayerbotAIConfig.h (botpch chain).
namespace ai::rest_gate_rt
{
// Bot thread. Episodes end at most once a fight, so one short std::mutex per event is enough.
void RecordEpisode(std::uint8_t cls, bool cohortB, ai::rest_gate::End end, std::uint32_t durMs);
void RecordCastFail(std::uint8_t cls, bool cohortB);   // a food / drink cast failed while resting (retry after CastRetryMs)

// One [RestGate] line: at most 300 a minute over all bots; false = no line (counted as dropped=).
bool TakeLineBudget(std::uint32_t now);

// World thread only. firstCall: the switch just went on - clear and start a window. Otherwise once per hour one [RestGateTotal] line per
// cohort and class with counts.
void Report(bool firstCall, std::uint32_t now);
}
