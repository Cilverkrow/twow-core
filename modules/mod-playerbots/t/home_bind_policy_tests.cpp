#include "HomeBindPolicy.h"

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
    using namespace ai::homebind;

    // Hillsbrad Foothills (Southshore) is a level 20-30 zone.
    Require(IsZoneClearlyAboveLevel(20, 3), "level 3 bot never binds or hearths to Southshore");
    Require(IsZoneClearlyAboveLevel(20, 11), "level 11 bot still too low for Hillsbrad");
    Require(!IsZoneClearlyAboveLevel(20, 15), "level 15 is within the usual +5 margin");
    Require(!IsZoneClearlyAboveLevel(1, 3), "starter zone stays a valid home");
    Require(!IsZoneClearlyAboveLevel(10, 5), "next zone within the margin stays valid");
    Require(!IsZoneClearlyAboveLevel(0, 1), "unknown zone level (custom zones) is never blocked");
    return 0;
}
