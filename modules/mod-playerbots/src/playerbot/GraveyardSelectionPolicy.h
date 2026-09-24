#pragma once

#include <cstdint>

namespace ai::graveyard_policy
{
// #276: a graveyard other than the one nearest the corpse (the alternate after
// repeated deaths, or the one near a far travel target) is only acceptable
// within a bounded distance of the corpse. Without the bound a level-8 bot that
// died in Elwynn could be revived in Hillsbrad by the 10-minute ghost teleport.
inline bool IsWithinAlternateDistance(float distanceToCorpse, float maxDistance)
{
    return distanceToCorpse >= 0.0f && maxDistance > 0.0f && distanceToCorpse <= maxDistance;
}

// Zone level must be known and not above the bot's level plus the usual margin.
// An unknown level (0) no longer passes as "low level".
inline bool IsZoneLevelAppropriate(std::uint32_t areaLevel, std::uint32_t botLevel)
{
    return areaLevel > 0 && areaLevel <= botLevel + 5;
}
}
