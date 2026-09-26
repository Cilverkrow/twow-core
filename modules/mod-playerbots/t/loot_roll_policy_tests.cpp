#include "LootRollPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

ai::loot_roll::RecipeFacts Facts(bool profession, bool known, bool inBags)
{
    ai::loot_roll::RecipeFacts facts;
    facts.hasProfession = profession;
    facts.known = known;
    facts.inBags = inBags;
    return facts;
}
}

int main()
{
    using namespace ai::loot_roll;

    // Owner, #341: need on an unknown recipe of an own profession, even when the
    // skill is still too low to learn it (no skill input on purpose).
    Decision d = ForRecipe(Facts(true, false, false));
    Require(d.vote == Vote::Need && !std::strcmp(d.reason, "recipe_new"), "own profession, unknown recipe: need");
    Require(ForRecipe(Facts(true, true, false)).vote == Vote::Pass, "already learned: pass");
    Require(ForRecipe(Facts(true, false, true)).vote == Vote::Pass, "already in the bags: pass");
    d = ForRecipe(Facts(false, false, false));
    Require(d.vote == Vote::Greed && !std::strcmp(d.reason, "recipe_no_profession"), "other profession: greed");

    // Gear: need only for a real upgrade; worse or unsuitable gear is greed.
    Require(ForGear(true, false).vote == Vote::Need, "upgrade: need");
    d = ForGear(false, true);
    Require(d.vote == Vote::Greed && !std::strcmp(d.reason, "not_upgrade"), "bad equip: greed, not need");
    Require(ForGear(false, false).vote == Vote::Unchanged, "other usages keep the usage vote");
    return 0;
}
