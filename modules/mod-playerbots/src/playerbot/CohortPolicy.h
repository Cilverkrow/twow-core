#pragma once

#include <cstdint>

namespace ai::cohort
{
// twow-repo#602 ROT-01: bot cohorts for an A/B inside one realm (OB-30 18.10.2026: one test copy, the arms are bot
// halves of the same realm). Pure rules, no game types, no locks, no config reads.
//
// A ROT switch that changes behaviour reads its config value as a mode:
//   0 = off (default), 1 = all bots, 2 = only cohort B, 3 = only cohort A. Any other value acts like 0.
// Mode 3 (twow-repo#601, OB-00 31.10.2026: crossover A/B designs - unit 2 swaps the guarded cohort, so each bot is its own
// control) is opt-in per key: a key whose loader clamps > 2 to 0 never passes 3 here, so for it nothing changes.
// Cohort B = odd low guid, cohort A = even low guid (the same parity rule as LootRolePolicy.h). A bot never changes
// its cohort, so the paired analysis can compare the same bots across arms.
constexpr std::uint32_t ModeOff = 0;
constexpr std::uint32_t ModeAll = 1;
constexpr std::uint32_t ModeOnlyB = 2;
constexpr std::uint32_t ModeOnlyA = 3;

inline bool InB(std::uint32_t guidLow)
{
    return (guidLow & 1u) != 0;
}

// True when a switch in `mode` applies to the bot with this low guid.
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return mode == ModeAll || (mode == ModeOnlyB && InB(guidLow)) || (mode == ModeOnlyA && !InB(guidLow));
}

// 'A' or 'B', for the cohort= field of the diagnostic lines.
inline char Letter(std::uint32_t guidLow)
{
    return InB(guidLow) ? 'B' : 'A';
}
}
