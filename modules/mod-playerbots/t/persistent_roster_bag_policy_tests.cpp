#include "PersistentRosterBagPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

using Slots = std::array<ai::roster::bags::BagSlotState, ai::roster::bags::kBagSlotCount>;

std::size_t Count(std::array<bool, ai::roster::bags::kBagSlotCount> const& plan)
{
    std::size_t count = 0;
    for (bool value : plan)
        count += value ? 1 : 0;
    return count;
}

void TestNonHunterEmptyAndExistingSlots()
{
    Slots empty{};
    CHECK(Count(ai::roster::bags::SelectEmptySlots(empty)) == 4);

    Slots mixed{};
    // An occupied slot represents an existing bag of any capacity, including
    // one that is larger than item 50004. It must never be selected.
    mixed[1].occupied = true;
    auto plan = ai::roster::bags::SelectEmptySlots(mixed);
    CHECK(!plan[1]);
    CHECK(Count(plan) == 3);
}

void TestFullAndIdempotent()
{
    Slots full{};
    for (auto& slot : full)
        slot.occupied = true;
    CHECK(Count(ai::roster::bags::SelectEmptySlots(full)) == 0);

    Slots initiallyEmpty{};
    auto firstPlan = ai::roster::bags::SelectEmptySlots(initiallyEmpty);
    CHECK(Count(firstPlan) == 4);
    for (std::size_t index = 0; index < initiallyEmpty.size(); ++index)
        if (firstPlan[index])
            initiallyEmpty[index].occupied = true;
    CHECK(Count(ai::roster::bags::SelectEmptySlots(initiallyEmpty)) == 0);
}

void TestHunterUsesAllEmptySlots()
{
    // Character class is not a policy input: a level-1 hunter with four
    // empty outer slots gets the same four normal bags as every roster bot.
    Slots hunterEmpty{};
    auto emptyPlan = ai::roster::bags::SelectEmptySlots(hunterEmpty);
    CHECK(Count(emptyPlan) == 4);

    Slots hunterWithQuiver{};
    hunterWithQuiver[2].occupied = true;
    auto quiverPlan = ai::roster::bags::SelectEmptySlots(hunterWithQuiver);
    CHECK(!quiverPlan[2]);
    CHECK(Count(quiverPlan) == 3);
}

void TestExistingSlotsAreNeverTargets()
{
    Slots existing{};
    existing[0].occupied = true;
    existing[3].occupied = true;
    auto plan = ai::roster::bags::SelectEmptySlots(existing);
    CHECK(!plan[0]);
    CHECK(!plan[3]);
    CHECK(plan[1]);
    CHECK(plan[2]);
}
}

int main()
{
    TestNonHunterEmptyAndExistingSlots();
    TestFullAndIdempotent();
    TestHunterUsesAllEmptySlots();
    TestExistingSlotsAreNeverTargets();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "PERSISTENT_ROSTER_BAG_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
