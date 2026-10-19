// twow-repo#601 R01 v4 (S1Cohort / S3Cohort): R01CohortPolicy.h - which bots a key group acts for.
#include "R01CohortPolicy.h"

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
    using ai::r01_cohort::Applies;

    // 0 and 1: every bot (the default, as before the cohort keys).
    for (std::uint32_t guid : {1u, 2u, 3u, 1000u, 1001u})
        Require(Applies(0, guid) && Applies(1, guid), "0 and 1 act for every bot");

    // 2: only cohort B (odd low guid), 3: only cohort A (even) - the parity rule of CohortPolicy.h.
    Require(Applies(2, 1) && Applies(2, 1001) && !Applies(2, 2) && !Applies(2, 1000), "2 = odd guids only");
    Require(Applies(3, 2) && Applies(3, 1000) && !Applies(3, 1) && !Applies(3, 1001), "3 = even guids only");
    Require(ai::cohort::InB(1) && !ai::cohort::InB(2), "cohort B is the odd guid, as in CohortPolicy.h");

    // 2 and 3 split the realm: every bot is in exactly one of them.
    for (std::uint32_t guid = 1; guid < 2000; ++guid)
        Require(Applies(2, guid) != Applies(3, guid), "2 and 3 are complementary");

    // Any other value: no bot (fail closed).
    for (std::uint32_t mode : {4u, 5u, 99u, 0xFFFFFFFFu})
        Require(!Applies(mode, 1) && !Applies(mode, 2), "unknown values act for no bot");

    std::cout << "r01_cohort_policy_tests passed\n";
    return 0;
}
