#pragma once

#include <cstdint>

#include "CohortPolicy.h"

namespace ai::r01_cohort
{
// twow-repo#601 R01 v4 (OB-00 19.10.2026: R01 as bot cohorts on two realms, parity swapped between the realms):
// AiPlayerbot.QuestFirstProgression.S1Cohort / S3Cohort say for which bots the S1 key group (FailureClasses,
// ResetOnProgressEvents, AbandonStructuralParked) and the S3 key group (QuestFeasibility.Enable with its lists,
// HubPreloopSameFilters, ZoneExit.TurnInTravelStall with TurnInNearRadius) act as their keys are set:
//   0 or 1 = every bot (default, as before), 2 = only cohort B (odd low guid), 3 = only cohort A (even low guid).
// Any other value: the group acts for no bot (fail closed). The cohort is the parity rule of CohortPolicy.h, so a bot
// never changes its cohort. The keys themselves stay global; a cohort value never switches a group on.
constexpr std::uint32_t ModeAll = 0;
constexpr std::uint32_t ModeAllToo = 1;
constexpr std::uint32_t ModeOnlyB = 2;
constexpr std::uint32_t ModeOnlyA = 3;

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return mode == ModeAll || mode == ModeAllToo || (mode == ModeOnlyB && ai::cohort::InB(guidLow)) ||
        (mode == ModeOnlyA && !ai::cohort::InB(guidLow));
}
}
