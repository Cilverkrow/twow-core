#include "ProfessionUsePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
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

// A recipe as the RealReagents scan fills it (twow-repo#485).
ai::profession_use::Recipe C(std::uint32_t spellId, std::uint32_t skillUpChance, std::uint32_t craftable)
{
    ai::profession_use::Recipe recipe = R(false, true, craftable > 0);
    recipe.spellId = spellId;
    recipe.skillUpChance = skillUpChance;
    recipe.craftable = craftable;
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

    // twow-repo#485: the best skill-up recipe from the bags, deterministic.
    Require(Pick({}) == -1, "no recipes: no pick");
    Require(Pick({ C(3276, 650, 5), C(3275, 1000, 1) }) == 1, "higher skill-up chance first (orange before green)");
    Require(Pick({ C(3275, 1000, 2), C(3276, 1000, 7) }) == 1, "same chance: more casts from the bags");
    Require(Pick({ C(3276, 1000, 2), C(3275, 1000, 2) }) == 1, "full tie: smaller spell id");
    {
        Recipe focus = C(2538, 1000, 9);
        focus.needsFocus = true;
        Recipe noTool = C(2660, 1000, 9);
        noTool.hasTools = false;
        Recipe empty = C(2963, 1000, 0);
        empty.hasReagents = true;   // craftable 0 alone keeps it out
        Recipe backedOff = C(3275, 1000, 9);
        backedOff.backedOff = true;
        Recipe grey = C(2329, 0, 9);
        grey.givesSkillUp = false;
        Recipe last = C(3276, 250, 1);
        Require(Pick({ focus, noTool, empty, backedOff, grey, last }) == 5,
            "skips spell focus, missing tool, craftable 0, backoff and no skill-up");
        Require(Pick({ focus, noTool, empty, backedOff, grey }) == -1, "no candidate left: -1");
    }
    {
        // Critic B4.1: Classify says None although every craftable recipe is
        // backed off; Pick must then say -1 (the trigger must not index).
        Recipe first = C(3275, 1000, 3);
        first.backedOff = true;
        Recipe second = C(3276, 850, 1);
        second.backedOff = true;
        Require(Classify({ first, second }) == CraftBlock::None, "Classify does not know the backoff");
        Require(Pick({ first, second }) == -1, "all backed off: no pick");
    }
    {
        // Review: a transmute on its 24-48 h category cooldown ranks first by
        // skill-up chance; the trigger marks it backed off, so the next recipe
        // is crafted instead of the transmute failing every interval.
        Recipe transmute = C(11479, 1000, 4);
        transmute.backedOff = true;
        Recipe potion = C(2330, 500, 3);
        Require(Pick({ transmute, potion }) == 1, "recipe on cooldown does not block the others");
    }

    // Critic B4.2: no craft cast while moving, fighting, casting, sitting or mounted.
    Require(IsIdleForCraft(false, false, false, false, false), "standing still: craft");
    Require(!IsIdleForCraft(true, false, false, false, false), "moving: wait");
    Require(!IsIdleForCraft(false, true, false, false, false), "in combat: wait");
    Require(!IsIdleForCraft(false, false, true, false, false), "casting: wait");
    Require(!IsIdleForCraft(false, false, false, true, false), "sitting (eating, drinking): wait");
    Require(!IsIdleForCraft(false, false, false, false, true), "mounted: wait");

    // Critic B4.6: vendor reagents come from AiPlayerbot.ProfessionUse.VendorReagents.
    std::set<std::uint32_t> const vendorReagents = { 3371, 3372, 2320, 2321, 2324, 2604, 2605, 6260, 6217, 2880, 3466, 2678, 2692, 3857 };
    Require(IsVendorReagent(vendorReagents, 3371), "Empty Vial is a vendor reagent");
    Require(!IsVendorReagent(vendorReagents, 2447), "Peacebloom is gathered, not bought");
    Require(!IsVendorReagent({}, 3371), "empty key: no vendor reagents (neutral default)");

    // Review: KeepCraftMaterials keeps below ReagentKeepStacks + 1 stacks; the
    // legacy "== 1" sold 21 linen (1.05 stacks) and the spec's 1.25 stacks.
    Require(KeepCraftStacks(1.0f, 1), "one full stack kept");
    Require(KeepCraftStacks(1.05f, 1), "21 linen kept with ReagentKeepStacks 1");
    Require(KeepCraftStacks(1.25f, 1), "1.25 stacks kept with ReagentKeepStacks 1");
    Require(!KeepCraftStacks(2.0f, 1), "two stacks: sold as before");
    Require(KeepCraftStacks(2.5f, 2), "2.5 stacks kept with ReagentKeepStacks 2");
    Require(!KeepCraftStacks(3.0f, 2), "three stacks with ReagentKeepStacks 2: sold");

    std::cout << "profession_use_policy_tests passed\n";
    return 0;
}
