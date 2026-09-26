#pragma once

#include <cstdint>
#include <ctime>
#include <vector>

namespace ai::profession_use
{
// #333: roster bots learned their professions (#306) but hardly used them:
// 15 herb/ore loots by 136 bots in six hours, no crafting at all. Nodes were
// only gathered within 15 yards, and crafting needed an RPG target.

// Passive gathering radius. A roster bot on its own may look further than the
// global GatheringDistance; groups keep their own group distances.
inline float GatherDistance(bool rosterSolo, float baseDistance, float rosterDistance)
{
    return rosterSolo && rosterDistance > baseDistance ? rosterDistance : baseDistance;
}

inline bool IsDue(std::time_t now, std::time_t last, std::uint32_t intervalSeconds)
{
    return last == 0 || now >= last + std::time_t(intervalSeconds);
}

struct Recipe
{
    bool needsFocus = false;     // forge, anvil, fire: stays with "rpg craft"
    bool givesSkillUp = false;
    bool hasReagents = false;
};

enum class CraftBlock : std::uint8_t
{
    None,
    NoRecipe,       // only focus recipes (or none) known
    NoSkillUp,      // every usable recipe is grey
    NoReagents,     // a skill-up recipe exists but the bags lack reagents
};

// The minimal crafting loop runs only for a recipe that needs no spell focus,
// still raises the skill and has its reagents in the bags.
inline CraftBlock Classify(std::vector<Recipe> const& recipes)
{
    bool usable = false, skillUp = false;
    for (Recipe const& recipe : recipes)
    {
        if (recipe.needsFocus)
            continue;
        usable = true;
        if (!recipe.givesSkillUp)
            continue;
        skillUp = true;
        if (recipe.hasReagents)
            return CraftBlock::None;
    }

    if (!usable)
        return CraftBlock::NoRecipe;
    return skillUp ? CraftBlock::NoReagents : CraftBlock::NoSkillUp;
}

inline char const* Name(CraftBlock block)
{
    switch (block)
    {
        case CraftBlock::NoRecipe: return "no_recipe";
        case CraftBlock::NoSkillUp: return "no_skillup_recipe";
        case CraftBlock::NoReagents: return "no_materials";
        default: return "none";
    }
}
}
