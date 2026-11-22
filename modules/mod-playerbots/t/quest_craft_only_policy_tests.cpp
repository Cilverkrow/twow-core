// twow-repo#750 (QuestFeasibility.CraftOnlySkip): craft-only classification, the held-count rule and the line shapes.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "QuestCraftOnlyPolicy.h"

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

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}
}

int main()
{
    using namespace ai::quest_craft_only;

    // Classification: a profession recipe and nothing else.
    Sources recipeOnly;
    recipeOnly.professionRecipe = true;
    Require(CraftOnly(recipeOnly), "recipe only (Shimmering Gold Necklace 41340) is craft-only");
    Require(!CraftOnly(Sources{}), "no source at all is not craft-only (not made by a profession)");
    Sources looted = recipeOnly;
    looted.loot = true;
    Require(!CraftOnly(looted), "a recipe item that also drops has another source");
    Sources sold = recipeOnly;
    sold.vendor = true;
    Require(!CraftOnly(sold), "a recipe item a vendor sells has another source");
    Sources rewarded = recipeOnly;
    rewarded.questGiven = true;
    Require(!CraftOnly(rewarded), "a recipe item a quest gives has another source");
    Sources questSpell = recipeOnly;
    questSpell.otherSpell = true;
    Require(!CraftOnly(questSpell), "a second, non-profession create spell (item use / event) is another source");
    Sources eventOnly;
    eventOnly.otherSpell = true;
    Require(!CraftOnly(eventOnly), "an event / item-use spell alone is not a profession item (Filled Crystal Phial)");
    // Multi-recipe: two profession recipes are still one flag - craft-only as long as nothing else makes it.
    Sources twoRecipes = recipeOnly;
    twoRecipes.professionRecipe = true;
    Require(CraftOnly(twoRecipes), "an item of two profession recipes stays craft-only");

    Require(IsProfessionCategory(11) && IsProfessionCategory(9), "professions and secondary skills");
    Require(!IsProfessionCategory(7) && !IsProfessionCategory(6) && !IsProfessionCategory(0), "class, weapon and other lines are not");

    // Held-count rule: the bot holds the count = feasible; one short = blocked at that objective.
    Need held[2] = { { 41366, 1, 1 }, { 41340, 1, 1 } };
    Require(FirstUnmet(held, 2) == -1, "holds every craft-only item: not blocked");
    Need missing[2] = { { 41366, 1, 1 }, { 41340, 1, 0 } };
    Require(FirstUnmet(missing, 2) == 1, "does not hold the necklace: blocked at objective 2");
    Need tooFew[1] = { { 1251, 10, 9 } };
    Require(FirstUnmet(tooFew, 1) == 0, "9 of 10 Linen Bandages is too few");
    Need enough[1] = { { 1251, 10, 12 } };
    Require(FirstUnmet(enough, 1) == -1, "more than the count is enough");
    Require(FirstUnmet(nullptr, 0) == -1, "a quest without craft-only items is never blocked");
    Need bothShort[2] = { { 1, 2, 0 }, { 2, 1, 0 } };
    Require(FirstUnmet(bothShort, 2) == 0, "the first unmet objective is named");

    // Lines.
    std::string const skip = FormatSkipLine(1864, 33, 41305, Path::Accept, missing[1]);
    Require(Has(skip, "[QuestFeasibility] state=skip bot=1864 level=33 quest=41305 reason=craft_only path=accept item=41340 held=0 required=1"),
        "accept skip line");
    Require(Has(FormatSkipLine(1, 2, 3, Path::Giver, missing[1]), "state=skip") && Has(FormatSkipLine(1, 2, 3, Path::Giver, missing[1]), "path=giver"),
        "giver skip line");
    std::string const held1 = FormatSkipLine(1, 2, 3, Path::Held, missing[1]);
    Require(Has(held1, "state=held") && !Has(held1, "state=skip") && Has(held1, "path=held"), "a held quest is not a skip");
    Require(FormatInitLine(253, 21, 1) == "[QuestCraftOnly] init mode=1 craft_only_items=253 quests=21", "init line");
    Require(FormatMinuteLine(4, 2, 1) == "[QuestCraftOnly] minute skips=4 giver_inactive=2 held_craft_only=1", "minute line");
    Require(ModeMax == 3, "modes 0-3");

    std::cout << "quest_craft_only_policy tests passed\n";
    return 0;
}
