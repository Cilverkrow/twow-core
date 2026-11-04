#pragma once

#include "DangerMapPolicy.h"
#include "PlayerbotAIConfig.h"

namespace ai::danger_map
{
// The danger map query/pool parameters from the configuration, one definition for every caller (it was copied three times: the
// death pool in PlayerbotAI.cpp, the travel choice in ChooseTravelTargetAction.cpp, the far-move arrival point in MovementActions.cpp).
// addDeathRule = the re-death rule at camps (AiPlayerbot.DangerMap.CountAddDeaths) applies to the asking bot; the caller decides that
// with ai::cohort::Applies(sPlayerbotAIConfig.dangerMapCountAddDeaths, guidLow).
inline Params ConfigParams(bool addDeathRule = false)
{
    Params params;
    params.cellSize = sPlayerbotAIConfig.dangerMapCellSize;
    params.windowSeconds = sPlayerbotAIConfig.dangerMapWindowSeconds;
    params.minDeaths = sPlayerbotAIConfig.dangerMapMinDeaths;
    params.levelMargin = sPlayerbotAIConfig.dangerMapLevelMargin;
    params.lineSamples = sPlayerbotAIConfig.dangerMapLineSamples;
    if (addDeathRule)
        params.addDeathMinAttackers = sPlayerbotAIConfig.dangerMapAddDeathMinAttackers;
    return params;
}
}
