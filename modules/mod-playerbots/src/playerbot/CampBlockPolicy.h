#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::camp_block
{
// twow-repo#602/#601 re-deaths at the same camp (CLI-ROT adds analysis, X3a 25.10.2026): 19.6 % of the deaths with adds came
// <= 30 min and <= 60 yd after the same bot's previous death - revive, back into the same camp, dead again. Half of them had
// another travel entry than the death before (mixed camps: Riverpaw Brute + Bandit) and 43 % no travel target at all, so neither
// the per-destination rule (#307 DestinationDeaths) nor GrindAvoid (per killer entry) saw them, and the danger map leaves out the
// bot's own cell and its neighbours. The key here is the death position: a bot keeps its last few deaths with several living
// attackers and stays away from destinations near them for a while. Pure rules, no game types, no locks, no config reads.
constexpr std::size_t MaxDeaths = 4;   // bounded per bot; a bot dies at only a few camps per half hour

struct Death
{
    std::uint32_t map = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t untilMs = 0;   // 0 = empty slot
};

struct Ring
{
    std::array<Death, MaxDeaths> deaths{};
    std::uint8_t next = 0;
};

// Millisecond timer math is unsigned, so a getMSTime wrap does not keep a block forever.
inline bool Active(Death const& death, std::uint32_t nowMs)
{
    return death.untilMs && std::int32_t(death.untilMs - nowMs) > 0;
}

// Counted only with at least minAttackers living attackers (adds + the target); 0 attackers never counts (no creature fight).
inline bool Counts(std::uint32_t attackers, std::uint32_t minAttackers)
{
    return attackers > 0 && attackers >= minAttackers;
}

// Records a death; the oldest slot is overwritten. seconds 0 = nothing recorded.
inline void Record(Ring& ring, std::uint32_t map, float x, float y, std::uint32_t nowMs, std::uint32_t seconds)
{
    if (!seconds)
        return;

    std::uint32_t until = nowMs + seconds * 1000u;
    if (!until)
        until = 1;
    ring.deaths[ring.next] = { map, x, y, until };
    ring.next = std::uint8_t((ring.next + 1) % MaxDeaths);
}

// True when (map, x, y) lies within radius (2D) of an active recorded death on the same map.
inline bool Blocked(Ring const& ring, std::uint32_t map, float x, float y, std::uint32_t nowMs, float radius)
{
    if (radius <= 0.0f)
        return false;

    float const radiusSq = radius * radius;
    for (Death const& death : ring.deaths)
    {
        if (!Active(death, nowMs) || death.map != map)
            continue;
        float const dx = death.x - x, dy = death.y - y;
        if (dx * dx + dy * dy <= radiusSq)
            return true;
    }
    return false;
}

// Quest destinations (objective, taker, giver) are never vetoed: a blocked one is kept as a fallback and taken only when the
// same choice selects nothing else. Every other destination near a recent camp death is skipped.
enum class Verdict { Free, Skip, Fallback };

inline Verdict Judge(bool blocked, bool questDestination)
{
    if (!blocked)
        return Verdict::Free;
    return questDestination ? Verdict::Fallback : Verdict::Skip;
}

// Lever 2 shadow (CLI-ROT 22.11.2026, OB-00 (a), OB-10 spec OK d947fa34): the mode of AiPlayerbot.DestinationDeaths.CampBlock.
// 0 off (default), 1 all bots, 2 cohort B only, 3 cohort A only (crossover), 4 shadow (every bot computes - records its ring and
// judges its choices - nobody skips or demotes). In modes 2 and 3 the other cohort computes in shadow. Any other value acts like 0.
constexpr std::uint32_t ModeShadow = 4;

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return mode != ModeShadow && ai::cohort::Applies(mode, guidLow);   // 1 all, 2 B, 3 A (CohortPolicy.h)
}

inline bool Computes(std::uint32_t mode)
{
    return mode >= ai::cohort::ModeAll && mode <= ModeShadow;
}

// The verdict a bot acts on: a shadow bot judges like an applied one (counted) but always goes on as if free.
inline Verdict Acted(Verdict judged, bool applies)
{
    return applies ? judged : Verdict::Free;
}

// [CampBlock] counters per cohort (0 = A, 1 = B), increments, taken once per minute by the world thread.
struct Counters
{
    std::atomic<std::uint64_t> choices{ 0 };           // finished destination choices of computing roster bots
    std::atomic<std::uint64_t> blockedSeen{ 0 };       // candidates near an active ring death (judged Skip or Fallback)
    std::atomic<std::uint64_t> skipped{ 0 };           // candidates skipped (applied)
    std::atomic<std::uint64_t> wouldSkip{ 0 };         // the same in shadow
    std::atomic<std::uint64_t> fallback{ 0 };          // quest destinations held back as fallback (applied)
    std::atomic<std::uint64_t> wouldFallback{ 0 };     // the same in shadow
    std::atomic<std::uint64_t> changed{ 0 };           // choices with at least one candidate skipped or held back (applied)
    std::atomic<std::uint64_t> wouldChange{ 0 };       // shadow choices whose selected destination was judged Skip
    std::atomic<std::uint64_t> wouldChangeQuest{ 0 };  // shadow choices whose selected destination was judged Fallback
    std::atomic<std::uint64_t> records{ 0 };           // deaths recorded into a ring
};

inline Counters counters[2];
}
