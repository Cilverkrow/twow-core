// twow-repo#601 owner rule R1 (ZoneKeepPolicy.h): StaleKeepZone and StartZoneMinLevel.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "ZoneKeepPolicy.h"

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
    using namespace ai::zone_keep;
    namespace sz = ai::start_zone;

    // 1) Start zone: playercreateinfo zone, the custom starts only with CustomRaceStartZones.
    {
        Require(InStartZone(1, false, 12, 12), "human in Elwynn (create zone 12)");
        Require(!InStartZone(1, false, 12, 40), "human in Westfall is outside");
        Require(!InStartZone(1, false, 0, 0), "unknown create zone is never 'inside'");
        Require(InStartZone(sz::GoblinRace, true, 14, sz::BlackstoneZone), "goblin start = Blackstone Island with custom starts");
        Require(!InStartZone(sz::GoblinRace, true, 14, 14), "goblin with custom starts: Durotar is outside");
        Require(InStartZone(sz::GoblinRace, false, 14, 14), "goblin without custom starts: the proxy start Durotar");
        Require(InStartZone(sz::HighElfRace, true, 12, sz::ThalassianZone) && InStartZone(sz::HighElfRace, true, 12, sz::AlahThalasZone),
            "high elf: Thalassian Highlands and Alah'Thalas");
        Require(StartZoneKnown(sz::GoblinRace, true, 0) && StartZoneKnown(1, false, 12) && !StartZoneKnown(1, false, 0), "known start");
    }

    // 2) StartZoneMinLevel: 0 off, gated below the level only.
    {
        Require(!StartZoneGated(0, 1), "0 = off");
        Require(StartZoneGated(5, 4) && !StartZoneGated(5, 5) && !StartZoneGated(5, 7), "owner value 5: L1-4 gated, L5+ free");
        Require(StartZoneDefers(true, true, false, 40), "gated, target outside the start zone: deferred");
        Require(!StartZoneDefers(true, true, true, 12), "gated, target inside: kept");
        Require(!StartZoneDefers(false, true, false, 40), "not gated: kept");
        Require(!StartZoneDefers(true, false, false, 40), "unknown start zone: fail open");
        Require(!StartZoneDefers(true, true, false, 0), "unknown target zone: kept");
    }

    // 3) StaleKeepZone: only an empty search with stale skips keeps the zone; the window; turn-ins exempt.
    {
        Require(KeepsZone(true, 1) && !KeepsZone(true, 0) && !KeepsZone(false, 3), "keeps only with the switch and a stale skip");
        Require(KeepActive(1000, 999) && !KeepActive(1000, 1000) && !KeepActive(0, 5), "window: until > now, 0 = none");
        Require(KeepActive(5u, 0xFFFFFFF0u), "window across the uint32 wrap");
        Require(KeepZoneDefers(true, false, 12, 40), "active, objective in another zone: deferred");
        Require(!KeepZoneDefers(true, true, 12, 40), "a turn-in in another zone stays allowed");
        Require(!KeepZoneDefers(true, false, 12, 12), "same zone: kept");
        Require(!KeepZoneDefers(false, false, 12, 40), "window over: kept");
        Require(!KeepZoneDefers(true, false, 0, 40) && !KeepZoneDefers(true, false, 12, 0), "unknown zone: kept");
    }

    std::cout << "zone_keep_policy_tests: all passed\n";
    return 0;
}
