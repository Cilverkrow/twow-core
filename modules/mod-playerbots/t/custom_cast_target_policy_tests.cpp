// twow-repo#741 (AUDIT-L03-5): the custom cast targets the bot only for item-target spells and caster-source-only spells.
// Inputs per spell as the core sees them in tw_world (pin 3b): Targets flags and SpellEntry::IsSpellWithCasterSourceTargetsOnly().
#include <cstdlib>
#include <iostream>

#include "CustomCastTargetPolicy.h"

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
    using namespace ai::custom_cast_target;

    // Fireball 133: Targets 0, implicit target A1 = 6 (enemy) -> not caster-source-only.
    Require(!UseBotAsTarget(false, false), "Fireball keeps the selection (Targets 0 is not self)");
    // Inner Fire: Targets 0, implicit target A1 = 1 (caster) -> caster-source-only.
    Require(UseBotAsTarget(false, true), "Inner Fire targets the bot");
    // An item-target spell (TARGET_FLAG_ITEM), e.g. a weapon enchant or disenchant: unchanged, the bot.
    Require(UseBotAsTarget(true, false), "an item-target spell keeps the bot as target");
    Require(UseBotAsTarget(true, true), "item flag and caster-source together: the bot");

    std::cout << "custom_cast_target_policy tests passed\n";
    return 0;
}
