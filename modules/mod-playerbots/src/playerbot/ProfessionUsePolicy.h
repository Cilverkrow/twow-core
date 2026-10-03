#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <set>
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
    // twow-repo#485 (AiPlayerbot.ProfessionUse.RealReagents): from the bags,
    // not from "can craft spell", which the item cheat makes true for all.
    std::uint32_t spellId = 0;
    std::uint32_t skillUpChance = 0; // per mille, as Player::UpdateCraftSkill rolls it
    std::uint32_t craftable = 0;     // casts the reagents in the bags pay for
    bool hasTools = true;            // every tool (SpellEntry::Totem) is in the bags
    bool backedOff = false;          // failed for good (reagent, tool, focus, no room) or on cooldown
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

// twow-repo#485: a craft cast fails while the bot moves (and the failed cast
// stops it), fights, casts, sits (the cast stands it up and ends a meal) or is
// mounted. Such a bot waits for the next check without using up the interval.
inline bool IsIdleForCraft(bool moving, bool inCombat, bool casting, bool sitting, bool mounted)
{
    return !moving && !inCombat && !casting && !sitting && !mounted;
}

// twow-repo#485: under the item cheat every recipe looked craftable, a random
// one was queued and the core failed the cast silently (v24: 4,791 starts,
// 0 "no_materials"). The bot now crafts a recipe without spell focus that
// still gives a skill-up and whose reagents and tools are in the bags; a recipe
// that failed on a missing reagent, tool, focus or room for the product waits
// out its backoff, one on (category) cooldown waits until it is ready again.
inline bool IsCandidate(Recipe const& recipe)
{
    return !recipe.needsFocus && recipe.givesSkillUp && recipe.hasReagents && recipe.hasTools &&
        recipe.craftable > 0 && !recipe.backedOff;
}

// Highest skill-up chance first (orange before grey), then the most casts the
// bags pay for, then the smallest spell id, so the pick is deterministic.
inline bool RanksBefore(Recipe const& a, Recipe const& b)
{
    if (a.skillUpChance != b.skillUpChance)
        return a.skillUpChance > b.skillUpChance;
    if (a.craftable != b.craftable)
        return a.craftable > b.craftable;
    return a.spellId < b.spellId;
}

// Index of the recipe to craft, -1 = none. Classify does not know the backoff:
// it says None even when every craftable recipe is backed off.
inline int Pick(std::vector<Recipe> const& recipes)
{
    int best = -1;
    for (std::size_t i = 0; i < recipes.size(); ++i)
    {
        if (!IsCandidate(recipes[i]))
            continue;
        if (best < 0 || RanksBefore(recipes[i], recipes[std::size_t(best)]))
            best = int(i);
    }
    return best;
}

// twow-repo#485: reagents bought from vendors (vials, thread, rods, flux)
// come from AiPlayerbot.ProfessionUse.VendorReagents, parsed once at config
// load; empty = none. Such a reagent counts as needed only once the other
// reagents of a known recipe are in the bags.
inline bool IsVendorReagent(std::set<std::uint32_t> const& vendorReagents, std::uint32_t itemId)
{
    return vendorReagents.find(itemId) != vendorReagents.end();
}

// twow-repo#485 (KeepCraftMaterials): from one stack on (below is "buy more")
// the materials of known recipes are kept below keepStacks + 1 stacks, like the
// reagent branch of the item usage (stacks < 2). The usage holds per item id:
// at or above the limit every stack of the item is sold as before, so the old
// "== 1" sold 1.25 stacks of linen.
inline bool KeepCraftStacks(float stacks, std::uint32_t keepStacks)
{
    return stacks < float(keepStacks) + 1.0f;
}
}
