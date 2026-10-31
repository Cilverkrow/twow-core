#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"
#include "GrindNeighbourPolicy.h"

namespace ai::safe_pull
{
// twow-repo#656 BOT-PULL-01-L1 (#653 lever 1, OB-00 31.10.2026; AiPlayerbot.Grind.SafePull, default 0). The #653 root cause
// (cand4r-B seg1, 231 fatal fights of the action-log bots): 73 % had 2+ attackers, but only 18 % of those were a pack pulled at
// once - 82 % had a late joiner (median +12 s) and 80 % of the late joiners hit the bot before it had touched them; 59 % of the
// fatal fights were opened by a creature that was not the bot's target. The bots do not pull packs, they fight where other
// creatures see them. The stage-2 grind pick (GrindTargetValue) takes the nearest candidate and never asks whether the bot's fight
// spot (melee: the target; ranged: its cast position) or its walk there lies in another creature's aggro radius. With the switch
// every stage-2 candidate gets a risk: the idle creatures of the pass's own list that would join a fight with it - linked to it
// (AGGRO_TOGETHER group, same-faction assist, call for help along its run; weight 2) or seeing the bot at the candidate, at the
// fight spot or on the approach line (weight 1). The pick is the candidate with the lowest risk, then the lowest score distance
// (the quest-need bonus stays in that distance: no hard quest veto). A bot below its ready HP / mana whose best candidate still
// has risk >= WaitRisk gets no target (it rests; attackers are stage 1 and untouched), at most MaxWaitS per episode. Pure rules,
// no game types.
//
// Mode: 0 off (default), 1 all bots, 2 cohort B only, 3 cohort A only (the crossover design, OB-00 31.10.2026: unit 1 runs mode 2,
// unit 2 mode 3, analysed within bot; the same meaning as CohortPolicy's new mode 3), 4 shadow only (every bot computes, nobody
// applies - the log-only run). In modes 2 and 3 the other cohort computes in shadow. Any other value acts like 0.
constexpr std::uint32_t ModeOnlyA = 3;
constexpr std::uint32_t ModeShadow = 4;

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    if (mode == ModeOnlyA)
        return !ai::cohort::InB(guidLow);
    return mode != ModeShadow && ai::cohort::Applies(mode, guidLow);
}

inline bool Computes(std::uint32_t mode)
{
    return mode >= ai::cohort::ModeAll && mode <= ModeShadow;
}

// Weights: a creature linked to the candidate joins for sure (it is pulled with it); one that only sees the bot joins if the fight
// stays in its radius long enough. A creature counts once, with its highest weight. The risk is capped (a dense camp is just "bad").
constexpr std::uint32_t LinkWeight = 2;
constexpr std::uint32_t SightWeight = 1;
constexpr std::uint32_t RiskCap = 16;

inline std::uint32_t Add(std::uint32_t risk, std::uint32_t weight)
{
    return std::min(RiskCap, risk + weight);
}

// The radii and the chase rule are GrindNeighbourPolicy's (who joins, Creature.cpp): same pads, same call-for-help run.
using ai::grind_neighbour::AssistPad;
using ai::grind_neighbour::SightPad;
using ai::grind_neighbour::CallForHelpPad;
using ai::grind_neighbour::Within;
using ai::grind_neighbour::ChaseDistSq;

// The fight spot: a ranged bot casts from SpotBack yd inside its pull range (the bot stops a little short), a melee bot fights at
// the candidate. Returns the spot's distance from the candidate towards the bot (0 = at the candidate); never beyond the bot.
constexpr float SpotBack = 5.0f;

inline float SpotOffset(float botDistance, float pullRange)
{
    if (pullRange <= 0.0f || botDistance <= 0.0f)
        return 0.0f;
    return std::max(0.0f, std::min(botDistance, pullRange - SpotBack));
}

// Squared 2D distance of the point (px, py) to the segment (ax, ay) - (bx, by): the approach line bot -> fight spot.
inline float SegmentDistSq(float px, float py, float ax, float ay, float bx, float by)
{
    float const vx = bx - ax;
    float const vy = by - ay;
    float const len = vx * vx + vy * vy;
    float t = len > 0.0f ? ((px - ax) * vx + (py - ay) * vy) / len : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    float const dx = px - (ax + t * vx);
    float const dy = py - (ay + t * vy);
    return dx * dx + dy * dy;
}

// Snapshot edge (as GrindNeighbourPolicy): a candidate beyond sight - 30 yd has part of its surroundings outside the list, so it is
// never "safe" - its risk is at least 1.
using ai::grind_neighbour::Edge;

inline std::uint32_t EdgeRisk(std::uint32_t risk, bool edge)
{
    return edge ? std::max<std::uint32_t>(risk, 1) : risk;
}

// The order: lower risk first, then the lower score distance. The level guard is GrindNeighbourPolicy's (at most one level above
// the distance pick, elite only if that one is) - the rule must not trade adds for a harder single target.
inline bool Better(std::uint32_t risk, float distance, std::uint32_t bestRisk, float bestDistance)
{
    return risk < bestRisk || (risk == bestRisk && distance < bestDistance);
}

using ai::grind_neighbour::GuardOk;

// Readiness (fail closed): a pick with risk >= WaitRisk only at HP >= readyHp % and, for a mana user, mana >= readyMana %.
// Otherwise no target, for at most MaxWaitS from the first refused pass of the episode; after that the best pick is taken.
constexpr std::uint32_t WaitRisk = 2;
constexpr std::uint32_t MaxWaitS = 90;

inline bool Ready(std::uint32_t hpPct, bool usesMana, std::uint32_t manaPct, std::uint32_t readyHp, std::uint32_t readyMana)
{
    return hpPct >= readyHp && (!usesMana || manaPct >= readyMana);
}

inline bool Waits(std::uint32_t risk, bool ready, std::uint32_t waitedS)
{
    return risk >= WaitRisk && !ready && waitedS < MaxWaitS;
}

// The last pick of a bot (applied or shadow), read at its death: [SafePull] death line. At most MaxCounted creatures of its
// counted set are kept (the guids, raw).
constexpr std::uint32_t MaxCounted = 8;
// A death later than this after the pick is not attributed to it.
constexpr std::uint32_t DeathWindowS = 300;

struct LastPick
{
    std::uint64_t target = 0;
    std::uint32_t at = 0;          // unix seconds
    std::uint32_t risk = 0;        // the rule's pick
    std::uint32_t pickRisk = 0;    // the distance pick
    bool applied = false;
    std::uint8_t counted = 0;
    std::uint64_t guids[MaxCounted] = {};
};

inline std::uint32_t CountedAmong(LastPick const& last, std::uint64_t const* guids, std::uint32_t n)
{
    std::uint32_t k = 0;
    for (std::uint32_t i = 0; i < n; ++i)
        for (std::uint8_t j = 0; j < last.counted; ++j)
            if (guids[i] && guids[i] == last.guids[j])
            {
                ++k;
                break;
            }
    return k;
}

// [SafePull] counters per cohort (0 = A, 1 = B), increments, taken once per minute by the world thread.
struct Counters
{
    std::atomic<std::uint64_t> passes{ 0 };       // stage-2 passes scored
    std::atomic<std::uint64_t> candidates{ 0 };   // candidates scored
    std::atomic<std::uint64_t> picks{ 0 };        // passes with a distance pick
    std::atomic<std::uint64_t> changed{ 0 };      // pick replaced (applied)
    std::atomic<std::uint64_t> wouldChange{ 0 };  // pick would be replaced (shadow)
    std::atomic<std::uint64_t> waits{ 0 };        // passes without a target because of the readiness rule (applied)
    std::atomic<std::uint64_t> wouldWait{ 0 };    // the same in shadow
    std::atomic<std::uint64_t> waitTimeout{ 0 };  // MaxWaitS reached: the risky pick taken
    std::atomic<std::uint64_t> pickSafe{ 0 };     // distance pick with risk 0
    std::atomic<std::uint64_t> ruleSafe{ 0 };     // rule pick with risk 0
    std::atomic<std::uint64_t> pickRisk{ 0 };     // sum of the distance picks' risk
    std::atomic<std::uint64_t> ruleRisk{ 0 };     // sum of the rule picks' risk
    std::atomic<std::uint64_t> edge{ 0 };         // candidates at the snapshot edge (risk at least 1)
    std::atomic<std::uint64_t> guarded{ 0 };      // passes where the level guard refused a lower-risk candidate
    std::atomic<std::uint64_t> extraYd{ 0 };      // sum of the extra bot distance of the replacements (yd, rounded)
    std::atomic<std::uint64_t> rkLink{ 0 };       // risk hits per kind, over all candidates
    std::atomic<std::uint64_t> rkTarget{ 0 };     // sees the bot at the candidate
    std::atomic<std::uint64_t> rkSpot{ 0 };       // sees the bot at the ranged fight spot
    std::atomic<std::uint64_t> rkPath{ 0 };       // sees the bot on the approach line
    std::atomic<std::uint64_t> capped{ 0 };       // passes with more than MaxEntries list entries
    std::atomic<std::uint64_t> micros{ 0 };       // time spent scoring (us)
    std::atomic<std::uint64_t> deaths{ 0 };       // [SafePull] death lines (2+ attackers, a pick within DeathWindowS)
};

inline Counters counters[2];

using ai::grind_neighbour::MaxEntries;

// Per cohort and class (1..11): picks, would change (shadow), changed (applied).
constexpr std::uint32_t MaxClass = 12;
inline std::atomic<std::uint64_t> classPicks[2][MaxClass];
inline std::atomic<std::uint64_t> classWould[2][MaxClass];
inline std::atomic<std::uint64_t> classChanged[2][MaxClass];
}
