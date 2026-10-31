// twow-repo#602 ROT-01: bot cohorts for an A/B inside one realm (CohortPolicy.h): mode 0 off, 1 all, 2 only cohort B.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "CohortPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
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
    using namespace ai::cohort;

    // 1) Mode numbers.
    {
        Require(ModeOff == 0 && ModeAll == 1 && ModeOnlyB == 2 && ModeOnlyA == 3, "mode numbers");
    }

    // 2) Cohort: odd low guid = B, even = A; guid 0 is A.
    {
        Require(!InB(0) && !InB(2) && !InB(1000), "even guids are cohort A");
        Require(InB(1) && InB(3) && InB(4294967295u), "odd guids are cohort B");
        Require(Letter(10) == 'A' && Letter(11) == 'B', "letters");
    }

    // 3) Applies: 0 never, 1 always, 2 only B, 3 only A (#601 crossover), any other value never (also a negative conf value
    //    read as uint32).
    {
        for (std::uint32_t guid : { 0u, 1u, 2u, 7u, 4294967294u, 4294967295u })
        {
            Require(!Applies(0, guid), "mode 0 is off for every bot");
            Require(Applies(1, guid), "mode 1 is on for every bot");
            Require(Applies(2, guid) == InB(guid), "mode 2 is on for cohort B only");
            Require(Applies(3, guid) == !InB(guid), "mode 3 is on for cohort A only");
            Require(Applies(2, guid) != Applies(3, guid), "modes 2 and 3 split every bot into exactly one arm");
            Require(!Applies(4, guid) && !Applies(0xFFFFFFFFu, guid), "unknown modes act like 0");
        }
    }

    // 4) Mode 2 splits a roster in two halves of the same bots every time.
    {
        unsigned on = 0;
        for (std::uint32_t guid = 1; guid <= 540; ++guid)
            on += Applies(2, guid) ? 1u : 0u;
        Require(on == 270, "540 consecutive guids: 270 in cohort B");
        unsigned onA = 0;
        for (std::uint32_t guid = 1; guid <= 540; ++guid)
            onA += Applies(3, guid) ? 1u : 0u;
        Require(onA == 270, "540 consecutive guids: 270 in cohort A (mode 3)");
    }

    std::cout << "cohort_policy_tests passed\n";
    return 0;
}
