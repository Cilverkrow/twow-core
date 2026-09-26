#pragma once

#include <cstdint>

namespace ai::loot_roll
{
// #341: group rolls of roster bots. The vote followed only "item usage": a
// worse or unsuitable item (BAD_EQUIP) was rolled need by every bot without a
// real-player master, and a recipe was at best greed - never need, and not
// even that while the bot's skill was still too low to learn it.
enum class Vote : std::uint8_t
{
    Unchanged,
    Need,
    Greed,
    Pass,
};

struct Decision
{
    Vote vote = Vote::Unchanged;
    char const* reason = "usage";
};

struct RecipeFacts
{
    bool hasProfession = false;  // the recipe's profession is learned
    bool known = false;          // the recipe spell is already learned
    bool inBags = false;         // a copy is already carried
};

// Need on an unknown recipe of an own profession, even while the skill is still
// too low to learn it; a duplicate is passed; other professions greed.
inline Decision ForRecipe(RecipeFacts const& facts)
{
    if (facts.known)
        return { Vote::Pass, "recipe_known" };
    if (facts.inBags)
        return { Vote::Pass, "recipe_duplicate" };
    if (facts.hasProfession)
        return { Vote::Need, "recipe_new" };
    return { Vote::Greed, "recipe_no_profession" };
}

// Need on gear only for a real upgrade (EQUIP); a worse or unsuitable item is
// greed. Everything else keeps the usage-based vote.
inline Decision ForGear(bool isEquipUpgrade, bool isBadEquip)
{
    if (isEquipUpgrade)
        return { Vote::Need, "upgrade" };
    if (isBadEquip)
        return { Vote::Greed, "not_upgrade" };
    return {};
}
}
