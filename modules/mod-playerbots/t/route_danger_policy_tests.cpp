#include "RouteDangerPolicy.h"

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
    using namespace ai::route_danger;

    // Live: level 2-4 bots in Durotar sent to a turn-in in Undercity (other continent).
    Require(Classify(true, 3, 10, 0) == Reason::CrossMap, "level 3 defers a cross-continent turn-in");
    Require(Classify(true, 10, 10, 0) == Reason::None, "from the minimum level the continent switch is allowed");
    Require(Classify(true, 3, 0, 0) == Reason::None, "0 disables the cross-map rule");

    Require(Classify(false, 5, 10, 20) == Reason::TargetZoneLevel, "same-continent target in a level 20 zone waits");
    Require(Classify(false, 15, 10, 20) == Reason::None, "within the +5 margin the zone is fine");
    Require(Classify(false, 1, 10, 0) == Reason::None, "unknown zone level (custom zones) is never blocked");
    Require(Classify(false, 3, 10, 5) == Reason::None, "neighbouring starter zone stays open");
    return 0;
}
