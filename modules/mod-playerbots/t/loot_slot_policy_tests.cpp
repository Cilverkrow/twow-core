#include "LootSlotPolicy.h"

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
    using namespace ai::loot_slot;

    // Packet layout: 3 normal items (slots 0-2), then the bot's quest items (3, 4).
    Require(!IsQuestSlot(0, 3) && !IsQuestSlot(2, 3), "normal slots come first");
    Require(IsQuestSlot(3, 3) && IsQuestSlot(4, 3), "quest slots follow the normal items");
    Require(IsQuestSlot(0, 0), "a loot with only quest items starts at slot 0");

    // Live #405: every quest item is is_blocked (FillQuestLoot reserves it for the
    // player); the shared check also fails for it. It must still be taken.
    Require(MayTake(true, true, false), "own quest item (blocked = reserved) is taken");
    Require(MayTake(true, false, true), "own quest item is taken");

    // Shared normal items keep the right check.
    Require(!MayTake(false, true, true), "a blocked shared item (roll) is not taken");
    Require(!MayTake(false, false, false), "a shared item without the right is not taken");
    Require(MayTake(false, false, true), "an allowed shared item is taken");

    std::cout << "loot_slot_policy_tests passed\n";
    return 0;
}
