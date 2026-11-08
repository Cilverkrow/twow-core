#include "LootChancePolicy.h"

#include <cstdint>
#include <cstdlib>
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
}

int main()
{
    using namespace ai::loot_chance;

    Require(!FixOn(0), "0 is the rollback");
    Require(FixOn(1), "1 is the fix (default)");
    Require(FixOn(2) && FixOn(-1) && FixOn(7), "an unknown mode acts like 1");

    // Creature entries are unchanged in both modes.
    Require(TemplateEntry(1, true) == 1u && TemplateEntry(1, false) == 1u, "creature entry 1");
    Require(TemplateEntry(3174, true) == 3174u, "creature entry as it is");

    // A game object guid without high guid overflow, over the whole entry range the guid can carry (24 bits).
    std::int32_t const goEntries[] = { -1, -2843, -153472, -179697, -0x00FFFFFF };
    for (std::int32_t e : goEntries)
    {
        std::uint64_t const fixed = PackGuid(HighGuidGameObject, TemplateEntry(e, true), 1);
        Require(HighOf(fixed) == HighGuidGameObject, "fix: the high guid stays HIGHGUID_GAMEOBJECT");
        Require(EntryOf(fixed) == std::uint32_t(-e), "fix: the guid carries the positive game object entry");
        Require((fixed & 0x00FFFFFFu) == 1u, "fix: counter 1");

        std::uint64_t const legacy = PackGuid(HighGuidGameObject, TemplateEntry(e, false), 1);
        Require(HighOf(legacy) != HighGuidGameObject, "rollback: the old guid overflows into the high guid (the bug)");
    }

    // The fix does not touch the creature guid.
    std::uint64_t const unit = PackGuid(HighGuidUnit, TemplateEntry(3174, true), 1);
    Require(HighOf(unit) == HighGuidUnit && EntryOf(unit) == 3174u, "creature guid unchanged");

    // Entry 0 (no game object): no overflow either way.
    Require(TemplateEntry(0, true) == 0u && TemplateEntry(0, false) == 0u, "entry 0");

    // QuestRelation.GameObjectChanceDrops: 0 by default, anything but 1 is 0.
    Require(!ChanceDropsOn(0) && ChanceDropsOn(1), "lever 0 / 1");
    Require(!ChanceDropsOn(2) && !ChanceDropsOn(-1), "an unknown lever value is 0");

    // Lever 0 = the rule as it was with the bug: a game object only through a quest item, whatever its (now right) chance.
    Require(IsObjectiveDrop(-2843, true, false, 0.0f), "game object, quest item: objective");
    Require(!IsObjectiveDrop(-2843, false, false, 16.67f), "lever 0: game object, other item, chance 16.67: no objective");
    Require(!IsObjectiveDrop(-2843, false, false, 100.0f), "lever 0: game object, other item, chance 100: no objective");
    Require(!AskChance(-2843, false), "lever 0: the chance of a game object is not asked");
    // Creatures unchanged in both lever modes.
    Require(IsObjectiveDrop(3174, false, false, 5.5f) && IsObjectiveDrop(3174, false, true, 5.5f), "creature above 5");
    Require(!IsObjectiveDrop(3174, false, false, 5.0f) && !IsObjectiveDrop(3174, false, true, 5.0f), "creature at 5: no");
    Require(IsObjectiveDrop(3174, true, false, 0.0f), "creature, quest item");
    // Lever 1: game objects by chance as creatures.
    Require(IsObjectiveDrop(-2843, false, true, 16.67f), "lever 1: game object above 5");
    Require(!IsObjectiveDrop(-4149, false, true, 5.0f), "lever 1: game object at 5: no");

    std::cout << "loot_chance_policy: all passed\n";
    return 0;
}
