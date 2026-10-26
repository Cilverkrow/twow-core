#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
}
