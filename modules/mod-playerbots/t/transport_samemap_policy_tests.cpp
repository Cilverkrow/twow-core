#include "TransportSameMapPolicy.h"

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
    using namespace ai::transport_samemap;

    // Guard on (default 1): the scan only for a dock on the bot's map.
    Require(ScanAllowed(true, 0, 0), "guard on: same map (Eastern Kingdoms) scans");
    Require(ScanAllowed(true, 1, 1), "guard on: same map (Kalimdor) scans");
    Require(!ScanAllowed(true, 0, 1), "guard on: dock on Eastern Kingdoms, bot on Kalimdor: no scan");
    Require(!ScanAllowed(true, 1, 0), "guard on: dock on Kalimdor, bot on Eastern Kingdoms: no scan");
    Require(!ScanAllowed(true, 0, 369), "guard on: bot in the Deeprun Tram map, dock on Eastern Kingdoms: no scan");
    Require(!ScanAllowed(true, 0xFFFFFFFFu, 0), "guard on: an unset dock map is another map: no scan");

    // Guard off (0 = rollback): always the scan, as before.
    Require(ScanAllowed(false, 0, 0), "guard off: same map scans");
    Require(ScanAllowed(false, 0, 1), "guard off: another map scans, as before");
    Require(ScanAllowed(false, 1, 0), "guard off: another map scans, as before (other direction)");

    std::cout << "transport_samemap_policy_tests: OK\n";
    return 0;
}
