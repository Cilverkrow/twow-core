#pragma once

#include <cstdint>

namespace ai::trainer_spell_key
{
// twow-repo AUDIT-L03 B7 (R2 bug fix, no switch): TrainableSpellMapValue files every trainer spell under
// (*spellMap)[trainerType][requirement]. For tradeskill trainers the requirement was only set after the dedup loop had
// already read (*spellMap)[trainerType][requirement]: the first spell of each list read an uninitialised value (UB), later
// spells the previous spell's skill, so the dedup looked in the wrong bucket and operator[] created junk buckets. The key is
// now computed per spell before the dedup loop, from the same sources as before. Pure rule, no game types; the trainer type
// values mirror the core's TrainerType (SharedDefines.h, static_assert at the call site).
constexpr std::uint32_t TypeClass = 0;
constexpr std::uint32_t TypeMounts = 1;
constexpr std::uint32_t TypeTradeskills = 2;
constexpr std::uint32_t TypePets = 3;

// learnedSkill: EffectMiscValue[1] of the trainer spell's learned spell; only read for tradeskill spells without reqSkill.
inline std::uint32_t Requirement(std::uint32_t trainerType, std::uint32_t trainerClass, std::uint32_t trainerRace,
    std::uint32_t reqSkill, std::uint32_t learnedSkill)
{
    switch (trainerType)
    {
        case TypeClass:
        case TypePets:
            return trainerClass;
        case TypeMounts:
            return trainerRace;
        case TypeTradeskills:
            return reqSkill ? reqSkill : learnedSkill;
        default:
            return 0;
    }
}

inline bool NeedsLearnedSkill(std::uint32_t trainerType, std::uint32_t reqSkill)
{
    return trainerType == TypeTradeskills && reqSkill == 0;
}
}
