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

inline bool IsEligibleProfessionTraining(bool persistentRosterMember,
    std::uint32_t level, std::uint32_t startLevel, profession::Pair pair,
    std::uint32_t skill)
{
    return IsEligible(persistentRosterMember, level, startLevel, pair) &&
        IsAllowedSkill(pair, skill);
}

// A profession plan is admission data, not a travel order. A registered bot
// may use a tradeskill trainer only when normal behaviour has already put it
// in local interaction range; it must never create a remote travel purpose
// merely because one of the two planned professions is missing.
inline bool MayRequestRemoteTrainerTravel(bool persistentRosterMember, bool tradeTrainer)
{
    return !(persistentRosterMember && tradeTrainer);
}

inline bool IsWithinLocalTrainerRadius(float distance, float radius)
{
    return distance >= 0.0f && radius >= 0.0f && distance <= radius;
}
}
