#pragma once

#include <algorithm>

namespace ai::reach_los
{
// twow-repo#594 (AiPlayerbot.Rotation.ReachLosClose, QFO-clean 23.10.2026): bot 1244 (priest, L5-6) stood 2 h in the Echo
// Ridge Mine 20.7 yd from a Kobold Laborer it could not see. ReachTargetAction::isUseful already forces a move without line
// of sight, but Execute kept the chase distance of a hostile target at 75 % of the spell range (about 22 yd) - the bot was
// already closer, ChaseTo did not move it, CheckReachProgress (hotfix 8.2) saw no progress and marked the target unreachable,
// and the next laborer took its place, for hours. Friendly targets already got min(distance x 0.9, preferred). Pure rules,
// no game types, no config reads.
//
// With the switch a hostile target out of sight gets min(distance x HostileFactor, preferred): the bot walks towards it until
// it sees it (each tick halves the distance again). If line of sight never comes (a wall), the bot makes no progress and
// CheckReachProgress still marks the target unreachable after its window - the stop condition stays as it was.
constexpr float FriendFactor = 0.9f;
constexpr float HostileFactor = 0.5f;

inline float ChaseDistance(bool inLos, bool isFriend, bool closeHostile, float distanceToTarget, float preferredDist)
{
    if (inLos)
        return preferredDist;
    if (isFriend)
        return std::min(distanceToTarget * FriendFactor, preferredDist);
    if (closeHostile)
        return std::min(distanceToTarget * HostileFactor, preferredDist);
    return preferredDist;
}
}
