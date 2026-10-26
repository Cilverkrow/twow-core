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

    // twow-repo#601 (cr2-C GO diagnosis): a quest item required by several quests is satisfied only when none needs more.
    {
        QuestItemNeed none;
        Require(!QuestItemSatisfied(none), "no quest requires it: not satisfied here (the loot strategy decides)");

        QuestItemNeed shard;   // Blood Shard: 5052 needs 1, 889 needs 10, the bot has 1
        AddQuestNeed(shard, 1, 1);
        AddQuestNeed(shard, 1, 10);
        Require(!QuestItemSatisfied(shard), "one quest has enough, the other needs more: taken");
        QuestItemNeed shardOtherOrder;   // slot order must not matter
        AddQuestNeed(shardOtherOrder, 1, 10);
        AddQuestNeed(shardOtherOrder, 1, 1);
        Require(!QuestItemSatisfied(shardOtherOrder), "same in the other slot order");

        QuestItemNeed done;
        AddQuestNeed(done, 10, 1);
        AddQuestNeed(done, 10, 10);
        Require(QuestItemSatisfied(done), "every quest has enough: not taken (as before)");

        QuestItemNeed single;
        AddQuestNeed(single, 3, 3);
        Require(QuestItemSatisfied(single), "one quest, enough: not taken (as before)");
        QuestItemNeed singleMore;
        AddQuestNeed(singleMore, 2, 3);
        Require(!QuestItemSatisfied(singleMore), "one quest, needs more: taken (as before)");
    }

    std::cout << "loot_slot_policy_tests passed\n";
    return 0;
}
