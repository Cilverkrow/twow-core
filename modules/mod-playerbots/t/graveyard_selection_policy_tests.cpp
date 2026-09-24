#include "GraveyardSelectionPolicy.h"

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
    using namespace ai::graveyard_policy;

    // Distances are rounded map yards: Goldshire -> Sentinel Hill (Westfall) is
    // about 1600, Goldshire -> Southshore (Hillsbrad) about 8600.
    Require(IsWithinAlternateDistance(1600.0f, 2500.0f), "neighbouring zone graveyard is acceptable");
    Require(!IsWithinAlternateDistance(8600.0f, 2500.0f), "level-8 Elwynn death never selects Hillsbrad");
    Require(IsWithinAlternateDistance(2500.0f, 2500.0f), "boundary is inclusive");
    Require(!IsWithinAlternateDistance(10.0f, 0.0f), "zero disables every alternate graveyard");
    Require(!IsWithinAlternateDistance(-1.0f, 2500.0f), "unknown distance is rejected");

    Require(IsZoneLevelAppropriate(10, 8), "zone within level + 5 is acceptable");
    Require(IsZoneLevelAppropriate(13, 8), "level + 5 boundary is acceptable");
    Require(!IsZoneLevelAppropriate(20, 8), "higher level zone is rejected");
    Require(!IsZoneLevelAppropriate(0, 8), "zone without a known level no longer passes");
    return 0;
}
