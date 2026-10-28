// twow-repo#333 finding 4 (TeleportCastPolicy.h, OB-00 27.10.2026): a travel step keeps the cast wait of the hearthstone or
// teleport it started, and waits for one the bot already prepares.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "TeleportCastPolicy.h"

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
    using namespace ai::teleport_cast;

    // 1) The spell ids of UseHearthStoneAction.
    Require(kHearthstone == 8690 && kScourgestone == 54403, "Hearthstone 8690, Scourgestone 54403");

    // 2) The same teleport: the step spell itself; the hearthstone node also takes the Scourgestone.
    Require(SameTeleport(8690, 8690), "hearthstone step, hearthstone prepared");
    Require(SameTeleport(8690, 54403), "hearthstone step, Scourgestone prepared");
    Require(SameTeleport(3567, 3567), "teleport spell step, the same spell prepared");
    Require(!SameTeleport(3567, 54403), "the Scourgestone only stands in for the hearthstone");
    Require(!SameTeleport(54403, 8690), "no reverse stand-in");
    Require(!SameTeleport(8690, 133), "another cast (Fireball) is not the teleport");
    Require(!SameTeleport(0, 8690) && !SameTeleport(8690, 0) && !SameTeleport(0, 0), "0 = no step spell or nothing prepared");

    // 3) The wait for a prepared teleport: the rest plus one react delay.
    Require(WaitMs(9900, 100) == 10000, "rest 9.9 s + 100 ms");
    Require(WaitMs(0, 100) == 100, "a cast at its end waits one react delay");

    // 4) The hold after a start: the wait from the spell itself; without a PREPARING spell the inner AI wait, at least one
    //    react delay (no stale outer value, no second jitter of a copied wait).
    Require(HoldMs(10100, 2525, 100) == 10100, "the spell's own wait wins over a jittered inner wait");
    Require(HoldMs(10100, 60000, 100) == 10100, "and over a larger stale value");
    Require(HoldMs(0, 10100, 100) == 10100, "an instant or finished cast: the inner wait");
    Require(HoldMs(0, 0, 100) == 100, "at least one react delay");

    std::cout << "teleport_cast_policy: all checks passed\n";
    return 0;
}
