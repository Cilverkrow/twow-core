#include "PersistentRosterStarterOutfitPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

void TestAdmissionAndLevelBoundary()
{
    CHECK(ai::roster::starter_outfit::ShouldProvision(true, 1));
    CHECK(!ai::roster::starter_outfit::ShouldProvision(false, 1)); // player/non-roster bot
    CHECK(!ai::roster::starter_outfit::ShouldProvision(true, 2));
}

void TestCompleteAndIdempotent()
{
    CHECK(ai::roster::starter_outfit::MissingAmount(1, 0) == 1);
    CHECK(ai::roster::starter_outfit::MissingAmount(1, 1) == 0);
    CHECK(ai::roster::starter_outfit::MissingAmount(20, 20) == 0);
    CHECK(ai::roster::starter_outfit::MissingAmount(20, 30) == 0);
    CHECK(ai::roster::starter_outfit::IsComplete(1, 1));
    CHECK(ai::roster::starter_outfit::IsComplete(20, 30));
}

void TestPartialRetryAndHunterAmmo()
{
    // Stackable items such as the canonical hunter ammunition entry use the
    // same exact missing-count rule as clothing and weapons.
    CHECK(ai::roster::starter_outfit::MissingAmount(200, 0) == 200);
    CHECK(ai::roster::starter_outfit::MissingAmount(200, 75) == 125);
    CHECK(!ai::roster::starter_outfit::IsComplete(200, 75));
    CHECK(ai::roster::starter_outfit::IsComplete(200, 200));

    // A full inventory causes the runtime store call to fail. No marker is
    // committed, so this unchanged missing amount is safe to retry later.
    CHECK(ai::roster::starter_outfit::MissingAmount(1, 0) == 1);
}
}

int main()
{
    TestAdmissionAndLevelBoundary();
    TestCompleteAndIdempotent();
    TestPartialRetryAndHunterAmmo();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "PERSISTENT_ROSTER_STARTER_OUTFIT_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
