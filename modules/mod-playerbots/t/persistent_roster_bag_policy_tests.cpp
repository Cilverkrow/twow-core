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
    CHECK(Count(ai::roster::bags::SelectEmptySlots(false, empty)) == 4);

    Slots mixed{};
    // An occupied slot represents an existing bag of any capacity, including
    // one that is larger than item 50004. It must never be selected.
    mixed[1].occupied = true;
    auto plan = ai::roster::bags::SelectEmptySlots(false, mixed);
    CHECK(!plan[1]);
    CHECK(Count(plan) == 3);
}

void TestFullAndIdempotent()
{
    Slots full{};
    for (auto& slot : full)
        slot.occupied = true;
    CHECK(Count(ai::roster::bags::SelectEmptySlots(false, full)) == 0);

    Slots initiallyEmpty{};
    auto firstPlan = ai::roster::bags::SelectEmptySlots(false, initiallyEmpty);
    CHECK(Count(firstPlan) == 4);
    for (std::size_t index = 0; index < initiallyEmpty.size(); ++index)
        if (firstPlan[index])
            initiallyEmpty[index].occupied = true;
    CHECK(Count(ai::roster::bags::SelectEmptySlots(false, initiallyEmpty)) == 0);
}

void TestHunterReserveAndRangedContainer()
{
    Slots hunterEmpty{};
    auto reservePlan = ai::roster::bags::SelectEmptySlots(true, hunterEmpty);
    CHECK(Count(reservePlan) == 3);
    CHECK(!reservePlan[3]);

    Slots hunterWithQuiver{};
    hunterWithQuiver[2].occupied = true;
    hunterWithQuiver[2].rangedContainer = true;
    auto quiverPlan = ai::roster::bags::SelectEmptySlots(true, hunterWithQuiver);
    CHECK(!quiverPlan[2]);
    CHECK(Count(quiverPlan) == 3);
}

void TestExistingSlotsAreNeverTargets()
{
    Slots existing{};
    existing[0].occupied = true;
    existing[3].occupied = true;
    auto plan = ai::roster::bags::SelectEmptySlots(false, existing);
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
    TestHunterReserveAndRangedContainer();
    TestExistingSlotsAreNeverTargets();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "PERSISTENT_ROSTER_BAG_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
