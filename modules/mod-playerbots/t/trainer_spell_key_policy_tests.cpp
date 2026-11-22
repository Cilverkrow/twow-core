// AUDIT-L03 B7: TrainableSpellMapValue bucket key per trainer spell, for all four trainer types and reqSkill 0.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "TrainerSpellKeyPolicy.h"

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
}

int main()
{
    using namespace ai::trainer_spell_key;

    // Class trainer (mage 8, race 0): key = class, whatever the spell's skill fields say.
    Require(Requirement(TypeClass, 8, 0, 0, 0) == 8, "class trainer keys by trainer class");
    Require(Requirement(TypeClass, 8, 0, 164, 333) == 8, "class trainer ignores reqSkill and learned skill");
    // Pet trainer (hunter 3): key = class.
    Require(Requirement(TypePets, 3, 0, 0, 0) == 3, "pet trainer keys by trainer class");
    // Mount trainer (race 6, class 0): key = race.
    Require(Requirement(TypeMounts, 0, 6, 762, 0) == 6, "mount trainer keys by trainer race");
    // Tradeskill trainer: recipe with reqSkill (Alchemy 171) -> reqSkill.
    Require(Requirement(TypeTradeskills, 0, 0, 171, 0) == 171, "tradeskill recipe keys by reqSkill");
    Require(Requirement(TypeTradeskills, 0, 0, 171, 186) == 171, "reqSkill wins over the learned skill");
    // Tradeskill trainer: rank spell with reqSkill 0 (Apprentice Miner) -> skill of the learned spell (Mining 186).
    Require(Requirement(TypeTradeskills, 0, 0, 0, 186) == 186, "tradeskill rank with reqSkill 0 keys by learned skill");
    // Unknown type: 0, never an indeterminate value.
    Require(Requirement(7, 8, 6, 171, 186) == 0, "unknown trainer type keys 0");

    // The learned skill is only looked up where it is used.
    Require(NeedsLearnedSkill(TypeTradeskills, 0), "tradeskill + reqSkill 0 needs the learned skill");
    Require(!NeedsLearnedSkill(TypeTradeskills, 171), "tradeskill + reqSkill does not");
    Require(!NeedsLearnedSkill(TypeClass, 0), "class trainer does not");
    Require(!NeedsLearnedSkill(TypeMounts, 0), "mount trainer does not");
    Require(!NeedsLearnedSkill(TypePets, 0), "pet trainer does not");

    std::cout << "trainer_spell_key_policy tests passed\n";
    return 0;
}
