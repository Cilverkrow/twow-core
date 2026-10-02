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

    // Hotfix 8.7: [Fishing] purpose trace.
    PurposeTrace trace;
    trace.OnCast(true);
    Require(!trace.ObserveChannel(true), "channel starts");
    Require(trace.ObserveChannel(false) && trace.channelBreaks == 1, "channel ended without a use: a lost catch");
    trace.OnCast(true);
    trace.ObserveChannel(true);
    trace.OnUse();
    Require(!trace.ObserveChannel(false) && trace.channelBreaks == 1, "channel ended after the use: a catch");
    trace.OnCast(false);
    Require(trace.casts == 2 && trace.castFailed == 1 && trace.useSent == 1, "counters");
    trace.Reset();
    Require(trace.casts == 0 && !trace.channel, "reset per purpose");

    std::cout << "fishing_policy_tests passed\n";
    return 0;
}
