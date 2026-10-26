#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::flee_away
{
// twow-repo#602 ROT-01 (c) (hunter analysis 25.10.2026, adds analysis; OB-00 go; AiPlayerbot.Rotation.FleeAway, default 0,
// BEHAVIOUR-CHANGING): a random bot's flee (MovementAction::Flee, the branch for bots without a real master) is a MoveChase of the
// enemy at the flee distance with angle = the bot's world angle to the enemy. The core adds the chase angle to the TARGET's facing
// (TargetedMovementGenerator: o + m_fAngle), so the flee point lies on an effectively random side of the mob - often across it or
// into its camp. The "already chasing" guard never matches (ServerFacade::GetChaseOffset returns 0), so MoveChase is re-issued
// with a new angle on every AI pass: the bot circles without gaining distance (hunter Grashnak: 560 flee dispatches, 5.7 yd from the
// engage point after 38 s, no damage) and adds join on the way (an add joined during a flee in about 10 % of the add-deaths). With
// the switch:
// - (a) the chase angle is relative to the enemy's facing, so the point lies straight away from it on the bot's side;
// - (b) a chase of the same enemy is kept for ReissueMs instead of being re-issued every pass (then re-aimed);
// - (c) a hunter whose pet holds the enemy flees only out of the 8 yd dead zone, not at 40 % of its spell range (it can shoot
//   from there; the generic rule kept it running from a mob that was not on it).
// Pure rules, no game types.

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// twow-repo#602 ROT-01 (c) scope (hunter restart A/B 26.10.2026, OB-00 decision): AiPlayerbot.Rotation.FleeAway.Scope. In the
// restart, cohort B's non-hunters (casters, warriors) died about 1.9x as often as cohort A's in the 4th hour, mostly with 2+
// attackers, spread over many bots - the switch applied to every bot without a real master. The scope limits (a) and (b) to the
// classes the analysis was about; a bot outside it takes the unchanged old chase. (c) is hunter-only anyway.
// 0 = every bot (as before, default), 1 = hunters only, 2 = hunters and bots with their pet out; unknown values act like 0.
inline bool InScope(std::uint32_t scope, bool hunter, bool petOut)
{
    switch (scope)
    {
        case 1:
            return hunter;
        case 2:
            return hunter || petOut;
        default:
            return true;
    }
}

constexpr float Pi = 3.14159265358979f;

// (a) The chase angle relative to the enemy's facing (both in radians, world frame), normalised to [0, 2 pi).
inline float RelativeAngle(float enemyToBot, float enemyFacing)
{
    float a = enemyToBot - enemyFacing;
    while (a < 0.0f)
        a += 2.0f * Pi;
    while (a >= 2.0f * Pi)
        a -= 2.0f * Pi;
    return a;
}

// (b) Keep the running chase of the same enemy for ReissueMs, then re-aim it.
constexpr std::uint32_t ReissueMs = 1000;

inline bool KeepChase(bool chasing, bool sameEnemy, std::uint32_t sinceIssueMs)
{
    return chasing && sameEnemy && sinceIssueMs < ReissueMs;
}

// (c) The hunter dead zone while the pet holds the enemy: Auto Shot needs 8 yd, so the flee is wanted only inside it plus 1 yd (the
// generic rule fled at 40 % of the spell range plus reach, about 13 yd for a hunter). The distance is the trigger's own, which
// includes the combat reach.
constexpr float DeadZone = 8.0f;
constexpr float DeadZoneMargin = 1.0f;

inline bool InDeadZone(float targetDistance, float combatReach)
{
    return targetDistance <= DeadZone + DeadZoneMargin + combatReach;
}

// [FleeAway] counters per cohort (0 = A, 1 = B), increments, taken once per minute by the world thread.
struct Counters
{
    std::atomic<std::uint64_t> issued{ 0 };      // flee chases issued with the relative angle
    std::atomic<std::uint64_t> kept{ 0 };        // flee passes that kept the running chase
    std::atomic<std::uint64_t> reaimed{ 0 };     // re-issues of the same enemy after ReissueMs
    std::atomic<std::uint64_t> deadZoneFlee{ 0 };   // hunter, pet holds the enemy: flee inside the dead zone
    std::atomic<std::uint64_t> deadZoneQuiet{ 0 };  // hunter, pet holds the enemy: no flee outside the dead zone
    std::atomic<std::uint64_t> outOfScope{ 0 };     // flee passes of a bot in the mode's cohort but outside Scope (old chase)
};

inline Counters counters[2];
}
