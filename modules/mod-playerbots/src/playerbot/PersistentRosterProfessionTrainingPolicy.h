#pragma once

#include "ProfessionPair.h"

#include <cstdint>

namespace ai::profession_training
{
constexpr std::uint32_t kCooking = 185;
constexpr std::uint32_t kFirstAid = 129;
constexpr std::uint32_t kFishing = 356;
// Turtle WoW Survival. This is a profession skill, never a hunter talent tab.
constexpr std::uint32_t kSurvival = 142;

inline bool IsSecondarySkill(std::uint32_t skill)
{
    for (std::uint32_t const secondary : { kCooking, kFishing, kFirstAid, kSurvival })
        if (skill == secondary)
            return true;
    return false;
}

inline bool IsEligible(bool persistentRosterMember, std::uint32_t level,
    std::uint32_t startLevel, profession::Pair pair)
{
    return persistentRosterMember && level >= startLevel && profession::IsValid(pair);
}

inline bool IsAllowedSkill(profession::Pair pair, std::uint32_t skill)
{
    return profession::Contains(pair, skill) || IsSecondarySkill(skill);
}

inline bool MayStartAutonomousTravel(bool hasRealPlayerMaster)
{
    return !hasRealPlayerMaster;
}
}
