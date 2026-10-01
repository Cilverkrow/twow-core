#include "FishingPolicy.h"

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
    using namespace ai::fishing;

    Require(KnownRank(false, false, false, true) == Apprentice, "an apprentice casts 7620, not 7731");
    Require(KnownRank(false, true, true, true) == Expert, "the highest known rank");
    Require(KnownRank(true, false, false, false) == Artisan, "artisan");
    Require(KnownRank(false, false, false, false) == 0, "no fishing spell");

    Require(!InGrace(0, 100), "no cast yet");
    Require(InGrace(100, 100) && InGrace(100, 100 + GraceSeconds - 1), "weapon stays off for 30 s after a cast");
    Require(!InGrace(100, 100 + GraceSeconds), "then done fishing may equip the weapon again");

    std::cout << "fishing_policy_tests passed\n";
    return 0;
}
