#include "FarFollowPolicy.h"

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
    using namespace ai::far_follow;

    Require(Decide(30.0f, 60.0f, 400.0f, true) == Decision::FOLLOW_UNIT, "within sight: unit follow");
    Require(Decide(60.0f, 60.0f, 400.0f, true) == Decision::FOLLOW_UNIT, "sight distance itself: unit follow");
    Require(Decide(95.0f, 60.0f, 400.0f, true) == Decision::WALK_REPLAN, "just beyond sight (after a summon): walk and re-plan");
    Require(Decide(400.0f, 60.0f, 400.0f, true) == Decision::WALK_REPLAN, "at the limit: still walk");
    Require(Decide(401.0f, 60.0f, 400.0f, true) == Decision::HOLD, "beyond the limit: hold");
    Require(Decide(8276.0f, 60.0f, 400.0f, true) == Decision::HOLD, "the train 5 continental walk holds");
    Require(Decide(8276.0f, 60.0f, 0.0f, true) == Decision::WALK_REPLAN, "0 keeps the old unbounded walk");
    Require(Decide(8276.0f, 60.0f, 400.0f, false) == Decision::WALK_REPLAN, "a bot master keeps the old behaviour");

    Require(ShouldTell(1000, 0, 300), "first time: tell");
    Require(!ShouldTell(1200, 1000, 300), "within five minutes: silent");
    Require(ShouldTell(1300, 1000, 300), "after five minutes: tell again");

    // GM check 3 C2: told 14:19:05 at Sentinel Hill, master landed in Stormwind ~2,100 yd away 3.5 min later.
    Require(ShouldTell(1210, 1000, 300, 2100.0f, 400.0f), "master flew away since the last request: tell again at once");
    Require(!ShouldTell(1210, 1000, 300, 150.0f, 400.0f), "master walked a little: still silent");
    Require(!ShouldTell(1210, 1000, 300, 400.0f, 400.0f), "exactly the hold distance: silent");
    Require(ShouldTell(1210, 1000, 300, 1.0e9f, 400.0f), "master on another map: tell again");
    Require(!ShouldTell(1210, 1000, 300, 2100.0f, 0.0f), "rearm distance 0: interval only");
    Require(ShouldTell(1000, 0, 300, 0.0f, 400.0f), "first time with rearm: tell");
    Require(ShouldTell(1300, 1000, 300, 0.0f, 400.0f), "rearm keeps the five-minute repeat");
    Require(ReplanMs > 0 && ReplanMs <= 5000, "a far walk is re-evaluated within seconds");
    return 0;
}
