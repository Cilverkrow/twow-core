#include "PersistentRosterTalentSpecPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

struct Path
{
    int id;
};

void TestRosterKeepsOnlyAClassValidStoredValue()
{
    std::vector<Path> warriorPaths{{0}, {1}, {2}};
    CHECK(ai::roster::talents::KeepStoredSpecNo(true, 1, warriorPaths));
    CHECK(ai::roster::talents::KeepStoredSpecNo(true, 2, warriorPaths));
    CHECK(ai::roster::talents::KeepStoredSpecNo(true, 3, warriorPaths));

    // Zero, an out-of-range value, and a value that belongs only to another
    // class all return false so the factory's existing weighted fallback runs.
    CHECK(!ai::roster::talents::KeepStoredSpecNo(true, 0, warriorPaths));
    CHECK(!ai::roster::talents::KeepStoredSpecNo(true, 4, warriorPaths));
    std::vector<Path> sparseOtherClassPaths{{0}, {2}};
    CHECK(!ai::roster::talents::KeepStoredSpecNo(true, 2, sparseOtherClassPaths));
}

void TestNonRosterKeepsExistingWeightedSelection()
{
    std::vector<Path> paths{{0}, {1}, {2}};
    CHECK(!ai::roster::talents::KeepStoredSpecNo(false, 2, paths));
}

void TestRepeatedPrepareDecisionIsIdempotent()
{
    std::vector<Path> paths{{0}, {1}, {2}};
    bool first = ai::roster::talents::KeepStoredSpecNo(true, 3, paths);
    bool second = ai::roster::talents::KeepStoredSpecNo(true, 3, paths);
    CHECK(first);
    CHECK(second);
}
}

int main()
{
    TestRosterKeepsOnlyAClassValidStoredValue();
    TestNonRosterKeepsExistingWeightedSelection();
    TestRepeatedPrepareDecisionIsIdempotent();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "PERSISTENT_ROSTER_TALENT_SPEC_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
