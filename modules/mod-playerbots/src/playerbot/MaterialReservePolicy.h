#pragma once

#include <cstdint>
#include <map>

namespace ai::material_reserve
{
// twow-repo#524 (owner 04.10.2026): "die bots müssen sich merken was sie benötigen rezepte grün
// und höher, diese materialien dürfen sie auch nicht verkaufen" and "schneider sollten ihren beruf
// erst bis wollstoff bringen und dann leinen verbände herstellen". One place decides what a recipe
// may use (UsableForRecipe, called from ai::CraftableFromBags for every craft path) and what must
// not be sold (Needed, used by ItemUsageValue).

constexpr std::uint32_t LinenCloth = 2589;
constexpr std::uint32_t SkillFirstAid = 129;
constexpr std::uint32_t SkillCooking = 185;
constexpr std::uint32_t SkillFishing = 356;
constexpr std::uint32_t MainNeedCasts = 5;   // a main profession keeps reagents for this many casts

// Secondary professions yield to the main ones.
inline bool IsSecondary(std::uint32_t skillId)
{
    return skillId == SkillFirstAid || skillId == SkillCooking || skillId == SkillFishing;
}

// Skill-up colour of a recipe as Player::UpdateCraftSkill sees it: green or better is any value
// below the recipe's grey level (skill_line_ability max_value), and below the rank cap.
inline bool GreenOrBetter(std::uint32_t skillValue, std::uint32_t skillMax, std::uint32_t recipeGreyAt)
{
    return skillValue < skillMax && skillValue < recipeGreyAt;
}

// What the bot's green-or-better recipes need, per reagent item.
struct Need
{
    std::uint32_t mainNeed = 0;     // reagents kept for main professions (MainNeedCasts casts)
    bool needed = false;            // some green-or-better recipe uses it: never sold below the keep limit
};
typedef std::map<std::uint32_t, Need> Memory;

inline void Remember(Memory& memory, std::uint32_t itemId, std::uint32_t perCast, std::uint32_t recipeSkillId)
{
    Need& need = memory[itemId];
    need.needed = true;
    if (!IsSecondary(recipeSkillId))
        need.mainNeed += perCast * MainNeedCasts;
}

struct Config
{
    std::uint32_t woolTierSkill = 75;          // tailoring below this keeps every linen
    std::uint32_t firstAidClothReserve = 20;   // from then on first aid only uses linen above this
};

enum class Reason : std::uint8_t
{
    Free,       // nothing reserved
    WoolRule,   // linen kept for tailoring
    MainFirst   // kept for a main profession's green-or-better recipes
};

inline char const* Name(Reason reason)
{
    switch (reason)
    {
        case Reason::WoolRule: return "wool_rule";
        case Reason::MainFirst: return "main_first";
        default: return "free";
    }
}

struct Decision
{
    std::uint32_t usable = 0;
    Reason reason = Reason::Free;
};

// How many of inBags a recipe of recipeSkillId may use. Main professions use everything; a
// secondary profession leaves linen to tailoring (wool rule) and the main professions' need.
inline Decision UsableForRecipe(std::uint32_t itemId, std::uint32_t inBags, std::uint32_t recipeSkillId,
    std::uint32_t mainNeed, bool knowsTailoring, std::uint32_t tailoringSkill, Config const& config)
{
    if (!IsSecondary(recipeSkillId))
        return { inBags, Reason::Free };

    if (itemId == LinenCloth && knowsTailoring)
    {
        if (tailoringSkill < config.woolTierSkill)
            return { 0, Reason::WoolRule };
        std::uint32_t const keep = config.firstAidClothReserve > mainNeed ? config.firstAidClothReserve : mainNeed;
        return { inBags > keep ? inBags - keep : 0, Reason::WoolRule };
    }

    if (mainNeed)
        return { inBags > mainNeed ? inBags - mainNeed : 0, Reason::MainFirst };

    return { inBags, Reason::Free };
}

// Selling: an item a green-or-better recipe needs is not sold below keepStacks + 1 stacks
// (above that the old rule sells, so the bags do not fill up).
inline bool MaySell(bool needed, float stacks, std::uint32_t keepStacks)
{
    return !needed || stacks >= float(keepStacks) + 1.0f;
}
}
