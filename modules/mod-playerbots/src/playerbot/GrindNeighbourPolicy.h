#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::grind_neighbour
{
// twow-repo#602 ROT-01 #2 (adds analysis 25.10.2026, OB-00 go; AiPlayerbot.Grind.NeighbourAware, default 0): 75 % of the X3a bot
// deaths had adds, 61 % of those adds came from the target's own camp (the same or a nearby creature type). The grind target pick
// (GrindTargetValue stage 2) chooses by distance only, so a bot pulls the camp centre as readily as a lone mob at its edge. With
// the switch the stage-2 pick is scored: W = the weighted neighbours that would join a fight with the candidate, and a candidate
// with fewer neighbours replaces the distance pick when distance + penalty(W) is lower. Demote only - no candidate is skipped, the
// pass returns a target whenever the old code does; an equal W keeps the old pick. Pure rules, no game types.
//
// Mode: 0 off (default), 1 all bots, 2 cohort B (cohort A computes in shadow), 3 shadow only (every bot computes, nobody applies -
// the log-only run). Any other value acts like 0.
constexpr std::uint32_t ModeShadow = 3;

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// The score is computed (and counted) for this bot: applied or shadow.
inline bool Computes(std::uint32_t mode)
{
    return mode >= ai::cohort::ModeAll && mode <= ModeShadow;
}

// Weights in half points: a grey neighbour 1 (it assists, but notices late and dies fast), a normal one 2, an elite or one at
// bot level + 3 or more 4. W is capped at 12 half points (6 neighbours) and each half point costs 5 yd, so a full pack costs at
// most 60 yd (critic: the first draft's cap 4 x 15 yd saturated in every dense camp).
constexpr std::uint32_t HalfCap = 12;
constexpr float YdPerHalf = 5.0f;

inline std::uint32_t HalfWeight(bool grey, bool strong)
{
    return strong ? 4u : (grey ? 1u : 2u);
}

inline float Penalty(std::uint32_t halfW)
{
    return float(std::min(halfW, HalfCap)) * YdPerHalf;
}

// Snapshot edge (critic): candidates and neighbours share the bot's "possible targets" list (sight distance around the bot), so a
// candidate near the edge has most of its neighbour disc outside it and looks cleaner than it is. Such a candidate never replaces
// the distance pick, and a distance pick at the edge is kept.
constexpr float EdgeMargin = 30.0f;

inline bool Edge(float botDistance, float sightDistance)
{
    return botDistance > sightDistance - EdgeMargin;
}

// Who joins (Creature.cpp): (a) assist - CallAssistance pulls the same faction template within CreatureFamilyAssistanceRadius on
// aggro; (b) on sight - a hostile creature within its own aggro distance of the fight; (c) group - an AGGRO_TOGETHER creature
// group at any distance; (d) call for help - a creature chasing its victim more than 10 yd from home calls every friendly creature
// (any faction template) within its call_for_help_range every second, i.e. along its run to a ranged bot.
constexpr float AssistPad = 2.0f;
constexpr float SightPad = 5.0f;
constexpr float CallForHelpPad = 2.0f;
constexpr float ChaseStart = 10.0f;

inline bool Within(float distSq, float radius)
{
    return radius > 0.0f && distSq <= radius * radius;
}

// (d): the candidate runs from its position towards the bot until it reaches the bot's pull position, pullRange yd away (a melee
// bot walks in, pullRange 0). It calls for help only on the part more than ChaseStart yd from its position. Returns the squared
// 2D distance of the point (px, py) to that part, relative to the candidate at the origin and the bot direction (ux, uy) (unit
// vector); a negative value when there is no such part.
inline float ChaseDistSq(float px, float py, float ux, float uy, float botDistance, float pullRange)
{
    float const end = std::min(botDistance, pullRange);
    if (end <= ChaseStart)
        return -1.0f;
    float t = px * ux + py * uy;
    t = std::max(ChaseStart, std::min(end, t));
    float const dx = px - t * ux;
    float const dy = py - t * uy;
    return dx * dx + dy * dy;
}

// Level guard (critic: no-add deaths are against harder targets, 41.5 % above the bot): a candidate that wins only by its W may be
// at most one level above the distance pick, and elite only if the distance pick is.
constexpr std::int32_t LevelSlack = 1;

inline bool GuardOk(std::int32_t level, bool elite, std::int32_t pickLevel, bool pickElite)
{
    return level <= pickLevel + LevelSlack && (!elite || pickElite);
}

// The replacement: strictly fewer neighbours and a strictly lower score than the distance pick.
inline bool Replaces(float distance, std::uint32_t halfW, float pickDistance, std::uint32_t pickHalfW)
{
    std::uint32_t const w = std::min(halfW, HalfCap);
    std::uint32_t const pw = std::min(pickHalfW, HalfCap);
    return w < pw && distance + Penalty(w) < pickDistance + Penalty(pw);
}

// At most this many list entries are neighbour candidates (the closest to the bot; the list is not sorted, so the first ones).
constexpr std::uint32_t MaxEntries = 64;

// [GrindNeighbour] counters per cohort (0 = A, 1 = B), increments, taken once per minute by the world thread.
struct Counters
{
    std::atomic<std::uint64_t> passes{ 0 };       // stage-2 passes scored
    std::atomic<std::uint64_t> candidates{ 0 };   // candidates scored
    std::atomic<std::uint64_t> picks{ 0 };        // passes with a distance pick
    std::atomic<std::uint64_t> changed{ 0 };      // pick replaced (applied)
    std::atomic<std::uint64_t> wouldChange{ 0 };  // pick would be replaced (shadow)
    std::atomic<std::uint64_t> edgePick{ 0 };     // distance pick at the snapshot edge (kept)
    std::atomic<std::uint64_t> edgeSkipped{ 0 };  // edge candidates kept out of the replacement
    std::atomic<std::uint64_t> guarded{ 0 };      // best replacement refused by the level guard
    std::atomic<std::uint64_t> churn{ 0 };        // the rule's pick differs from the last pass while that one is still a candidate
    std::atomic<std::uint64_t> churnDistance{ 0 };   // the same for the distance pick (the old rule's own churn, the baseline)
    std::atomic<std::uint64_t> capped{ 0 };       // passes with more than MaxEntries list entries
    std::atomic<std::uint64_t> pickHalfW{ 0 };    // sum of the distance picks' W (half points)
    std::atomic<std::uint64_t> newHalfW{ 0 };     // sum of the replacements' W (applied or shadow)
    std::atomic<std::uint64_t> extraYd{ 0 };      // sum of the extra distance of the replacements (yd, rounded)
    std::atomic<std::uint64_t> nbAssist{ 0 };     // neighbour hits per kind, over all candidates
    std::atomic<std::uint64_t> nbSight{ 0 };
    std::atomic<std::uint64_t> nbGroup{ 0 };
    std::atomic<std::uint64_t> nbCallForHelp{ 0 };
    std::atomic<std::uint64_t> micros{ 0 };       // time spent scoring (us)
};

inline Counters counters[2];

// Per cohort and class (1..11) for the stratified readout: picks, would change (shadow), changed (applied).
constexpr std::uint32_t MaxClass = 12;
inline std::atomic<std::uint64_t> classPicks[2][MaxClass];
inline std::atomic<std::uint64_t> classWould[2][MaxClass];
inline std::atomic<std::uint64_t> classChanged[2][MaxClass];
}
