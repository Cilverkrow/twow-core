#pragma once

namespace ai::grind_crowd
{
// AiPlayerbot.Grind.CrowdPenalty (bug fix with a switch, OB-10 27.11.2026; default 0 until its acceptance, then 1).
// The grind pick scores a candidate by distance in yards (lower is better). For a candidate that more bots of the own group
// target than the assist allows, the intent is a penalty of YardsPerBot per targeting bot. The old line assigned that
// penalty to the score ("=+") instead of adding it, so a crowded candidate looked YardsPerBot x count yards away, however
// far it really was. additive = false keeps that behaviour exactly; true adds the penalty to the distance.
constexpr float YardsPerBot = 5.0f;

inline float CrowdedDistance(float distance, int targeting, bool additive)
{
    float const penalty = float(targeting) * YardsPerBot;
    return additive ? distance + penalty : penalty;
}
}
