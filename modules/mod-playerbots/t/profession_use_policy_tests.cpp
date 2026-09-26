#include "ProfessionUsePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

ai::profession_use::Recipe R(bool focus, bool skillUp, bool reagents)
{
    ai::profession_use::Recipe recipe;
    recipe.needsFocus = focus;
    recipe.givesSkillUp = skillUp;
    recipe.hasReagents = reagents;
    return recipe;
}
}

int main()
{
    using namespace ai::profession_use;

    // Gathering radius: live 15 yd; roster bots on their own may look further.
    Require(GatherDistance(true, 15.0f, 40.0f) == 40.0f, "roster bot on its own uses the wider radius");
    Require(GatherDistance(false, 15.0f, 40.0f) == 15.0f, "other bots keep GatheringDistance");
    Require(GatherDistance(true, 15.0f, 0.0f) == 15.0f, "0 keeps GatheringDistance");
    Require(GatherDistance(true, 30.0f, 20.0f) == 30.0f, "never smaller than GatheringDistance");

    Require(IsDue(1000, 0, 300), "first attempt is due");
    Require(!IsDue(1299, 1000, 300), "interval not yet over");
    Require(IsDue(1300, 1000, 300), "interval over");

    // Crafting: only a skill-up recipe without spell focus and with reagents.
    Require(Classify({ R(false, true, true) }) == CraftBlock::None, "bandage with linen: craft");
    Require(Classify({ R(true, true, true) }) == CraftBlock::NoRecipe, "forge recipes stay with rpg craft");
    Require(Classify({}) == CraftBlock::NoRecipe, "no recipes");
    Require(Classify({ R(false, false, true) }) == CraftBlock::NoSkillUp, "grey recipes do not craft");
    Require(Classify({ R(false, true, false) }) == CraftBlock::NoReagents, "skill-up recipe without materials");
    Require(Classify({ R(false, true, false), R(false, true, true) }) == CraftBlock::None, "any craftable recipe is enough");
    Require(std::string(Name(CraftBlock::NoReagents)) == "no_materials", "reason names match #333");
    Require(std::string(Name(CraftBlock::NoSkillUp)) == "no_skillup_recipe", "reason names match #333");
    return 0;
}
