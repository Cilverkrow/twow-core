#pragma once

namespace ai::far_follow
{
// twow-repo#303 part 2. Train 5 snapshot: bots of a real player walked up to
// 8,276 yd across the continent to their master (wander/follow -> far branch
// of Follow(), which walks to a snapshot of the master's position). A
// low-level bot on that walk is a death risk, and a point walk ignores a
// master who turns around.

enum class Decision
{
    FOLLOW_UNIT,   // within sight: normal unit follow (tracks the master)
    WALK_REPLAN,   // beyond sight, within the limit: walk, but re-plan soon
    HOLD,          // beyond the limit: do not walk; own activity continues
};

// maxWalkDistance 0 keeps the old unbounded walk.
inline Decision Decide(float distance, float sightDistance, float maxWalkDistance, bool targetIsRealPlayer)
{
    if (distance <= sightDistance)
        return Decision::FOLLOW_UNIT;
    if (targetIsRealPlayer && maxWalkDistance > 0.0f && distance > maxWalkDistance)
        return Decision::HOLD;
    return Decision::WALK_REPLAN;
}

// A far walk towards a real player is re-evaluated after this long instead of
// waiting until the old point is reached.
constexpr unsigned int ReplanMs = 2000;

// "Too far away, summon me" at most once per interval per bot.
inline bool ShouldTell(long long now, long long lastTold, long long intervalSeconds)
{
    return lastTold <= 0 || now - lastTold >= intervalSeconds;
}
}
