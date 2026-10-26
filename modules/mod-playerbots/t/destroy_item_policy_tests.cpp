#include "DestroyItemPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

using Positions = std::vector<std::size_t>;

// OB-10 26.10.: two stacks of one grey item - the case of the use after free.
void TestTwoStacksOfOneId()
{
    CHECK(ai::destroy_item::FirstOfEachId({4865, 4865}) == Positions({0}));
    CHECK(ai::destroy_item::FirstOfEachId({4865, 2589, 4865}) == Positions({0, 1}));
    CHECK(ai::destroy_item::FirstOfEachId({2589, 4865, 4865, 2589, 4865}) == Positions({0, 1}));
}

void TestDistinctIdsKeepTheirOrder()
{
    CHECK(ai::destroy_item::FirstOfEachId({}).empty());
    CHECK(ai::destroy_item::FirstOfEachId({7}) == Positions({0}));
    CHECK(ai::destroy_item::FirstOfEachId({30, 10, 20}) == Positions({0, 1, 2}));
}
}

int main()
{
    TestTwoStacksOfOneId();
    TestDistinctIdsKeepTheirOrder();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "DESTROY_ITEM_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
