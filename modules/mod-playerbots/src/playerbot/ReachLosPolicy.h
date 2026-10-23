#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include "CohortPolicy.h"

namespace ai::reach_los
{
// twow-repo#594 (AiPlayerbot.Rotation.ReachLosClose, QFO-clean 23.10.2026): bot 1244 (priest, L5-6) stood 2 h in the Echo
// Ridge Mine 20.7 yd from a Kobold Laborer it could not see. ReachTargetAction::isUseful already forces a move without line
// of sight, but Execute kept the chase distance of a hostile target at 75 % of the spell range (about 22 yd) - the bot was
// already closer, ChaseTo did not move it, CheckReachProgress (hotfix 8.2) saw no progress and marked the target unreachable,
// and the next laborer took its place, for hours. Friendly targets already got min(distance x 0.9, preferred). Pure rules,
// no config reads.
//
// Mode (CohortPolicy.h, in-realm A/B; CLI-ROT review): 0 = off (default), 1 = every bot, 2 = only cohort B (odd low guid).
// With the mode a hostile target out of sight gets max(distance x HostileFactor, min(preferred, floor)): the bot walks
// towards it until it sees it (each tick halves the distance again), but never closer than the floor - the spell's minimum
// range (hunter dead zone) or, without one, tooCloseDistance. If line of sight never comes (a wall) or the bot already stands
// inside the floor, it makes no progress and CheckReachProgress still marks the target unreachable after its window - the
// stop condition stays as it was. Known: once line of sight returns the bot keeps the shorter distance (no back-off), as for
// friendly targets today.
constexpr float FriendFactor = 0.9f;
constexpr float HostileFactor = 0.5f;
constexpr std::uint32_t InLosAfterSeconds = 5;   // a "closed" evaluation counts as in_los_after if line of sight came within this

enum class Kind : std::uint8_t
{
    Unchanged,   // in sight, friendly, or the mode does not apply: the old rule
    Closed,      // hostile out of sight: chased closer than preferred
    Floored,     // hostile out of sight, the floor is above distance x HostileFactor: the bot walks in to the floor
    Stood        // hostile out of sight and the bot already stands at or inside the floor: it does not move (8.2 ends it)
};

struct Chase
{
    float distance = 0.0f;
    Kind kind = Kind::Unchanged;
};

inline Chase ChaseDistance(bool inLos, bool isFriend, bool closeHostile, float distanceToTarget, float preferredDist, float floorDist)
{
    if (inLos)
        return { preferredDist, Kind::Unchanged };
    if (isFriend)
        return { std::min(distanceToTarget * FriendFactor, preferredDist), Kind::Unchanged };
    if (!closeHostile)
        return { preferredDist, Kind::Unchanged };

    float const floor = std::min(preferredDist, floorDist);
    float const closer = distanceToTarget * HostileFactor;
    if (distanceToTarget <= floor)
        return { floor, Kind::Stood };
    if (closer < floor)
        return { floor, Kind::Floored };
    return { std::min(closer, preferredDist), Kind::Closed };
}

// The mode applies to this bot (0 off, 1 all, 2 cohort B; anything else acts like 0).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// The floor: the qualified spell's minimum range when it has one, else tooCloseDistance.
inline float Floor(float spellMinRange, float tooCloseDistance)
{
    return spellMinRange > 0.0f ? spellMinRange : tooCloseDistance;
}

// [ReachLos] minute counters (CLI-ROT: mechanism proof per cohort; closed_A stays 0 with mode 2 by construction - a consistency
// check, not a leak proof; closed_* count per evaluation, episodes / in_los_after per episode). Map threads
// add, the world thread takes them once per minute.
inline std::atomic<std::uint32_t> closedA{ 0 };
inline std::atomic<std::uint32_t> closedB{ 0 };
inline std::atomic<std::uint32_t> floored{ 0 };     // walk-in to the floor
inline std::atomic<std::uint32_t> stood{ 0 };       // already at or inside the floor (CLI-ROT re-review)
inline std::atomic<std::uint32_t> episodes{ 0 };    // closed evaluations that started an episode (losClosedAt was 0): the in_los_after denominator
inline std::atomic<std::uint32_t> inLosAfter{ 0 };  // episodes whose target came into sight within InLosAfterSeconds (Execute or isUseful)

inline std::string FormatMinuteLine(std::uint32_t mode, std::uint32_t a, std::uint32_t b, std::uint32_t f, std::uint32_t s,
    std::uint32_t e, std::uint32_t l)
{
    return "[ReachLos] minute mode=" + std::to_string(mode) + " closed_A=" + std::to_string(a) + " closed_B=" + std::to_string(b) +
        " floored=" + std::to_string(f) + " stood=" + std::to_string(s) + " episodes=" + std::to_string(e) + " in_los_after=" +
        std::to_string(l);
}
}
