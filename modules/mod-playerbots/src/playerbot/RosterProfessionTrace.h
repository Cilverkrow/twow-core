#pragma once

#include "Common.h"

#include <ctime>
#include <map>
#include <string>

class Player;

namespace ai
{
// Throttled, level-1-visible diagnostics for the roster profession path. Shared
// by the RPG trigger (why a nearby trainer is or is not a target) and the action
// (what the visit achieved). Off unless AiPlayerbot.ProfessionTraining.Trace = 1.
class RosterProfessionTraceGate
{
public:
    void Emit(Player* bot, uint32 pair, char const* stage, char const* state, char const* reason,
        uint32 trainerEntry, float distance);

private:
    std::map<std::string, std::time_t> nextEmitAt;
};
}
