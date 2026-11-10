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
        // SZM adoption review (10.11.2026): without CustomRaceStartZones a goblin / high elf lives at its proxy start while its
        // playercreateinfo zone is the custom one (5536 / 5225): unknown, so the gate fails open (not held, no rescue_gate).
        Require(!StartZoneKnown(sz::GoblinRace, false, sz::BlackstoneZone) && !StartZoneKnown(sz::HighElfRace, false, sz::ThalassianZone),
            "custom start race without custom starts: unknown (fail open)");
        Require(StartZoneKnown(sz::GoblinRace, true, sz::BlackstoneZone) && StartZoneKnown(sz::HighElfRace, true, sz::ThalassianZone),
            "custom start race with custom starts: known");
        Require(StartZoneDefers(true, StartZoneKnown(sz::GoblinRace, false, sz::BlackstoneZone), false, 14) == false,
            "goblin in Durotar without custom starts: no deferral");
        Require(CustomStartUngated(sz::GoblinRace, false) && CustomStartUngated(sz::HighElfRace, false),
            "custom start race without custom starts: never held (StartZoneHolds, band-step hold)");
        Require(!CustomStartUngated(sz::GoblinRace, true) && !CustomStartUngated(sz::HighElfRace, true), "with custom starts: held as any race");
        for (std::uint32_t race = 1; race <= 8; ++race)
            Require(!CustomStartUngated(race, false) && !CustomStartUngated(race, true), "standard races: never ungated");
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

    // 4) Package "start-zone rule complete" (StartZoneMinLevel.Complete): the switch, G1 follow, G3 leave clock, V2-a release push.
    {
        Require(CompleteOn(1) && !CompleteOn(0) && !CompleteOn(2) && !CompleteOn(-1), "switch: 1 on, everything else off");
        Require(FollowDefers(true, true), "G1: held, leader outside the start zone: no follow");
        Require(!FollowDefers(true, false) && !FollowDefers(false, true), "G1: leader inside, or not held: follow as before");

        // G3: the refusal run and the leave after FollowLeaveSeconds of fresh refusals.
        Require(NextRefusedSince(0, 0, 1000) == 1000, "G3: the first refusal starts the run");
        Require(NextRefusedSince(1000, 1030, 1050) == 1000, "G3: a fresh refusal keeps the run");
        Require(NextRefusedSince(1000, 1030, 1030 + RefusalFreshSeconds + 1) == 1030 + RefusalFreshSeconds + 1,
            "G3: a stale last refusal starts a new run");
        Require(!LeaveDue(1000, 1299, 1299, FollowLeaveSeconds), "G3: 299 s refused: stay");
        Require(LeaveDue(1000, 1300, 1300, FollowLeaveSeconds), "G3: 300 s refused in a row: leave");
        Require(!LeaveDue(1000, 1300, 1300 + RefusalFreshSeconds + 1, FollowLeaveSeconds), "G3: the refusals stopped: no leave");
        Require(!LeaveDue(0, 1300, 1400, FollowLeaveSeconds) && !LeaveDue(1000, 0, 1400, FollowLeaveSeconds), "G3: no run: no leave");
        Require(LeaveDue(0xFFFFFF00u, 0x0000002Cu, 0x0000002Cu, FollowLeaveSeconds), "G3: across the uint32 wrap");
        Require(LeaveAllowed(false) && !LeaveAllowed(true), "G3: never a group with a real player in it (OB-00 (b))");

        // V2-a: the release window StartZoneMinLevel .. +2, only in the start zone, the push at least one prior failure.
        Require(!InReleaseWindow(5, 4) && InReleaseWindow(5, 5) && InReleaseWindow(5, 7) && !InReleaseWindow(5, 8), "V2-a: L5-7");
        Require(!InReleaseWindow(0, 5), "V2-a: StartZoneMinLevel 0: never");
        Require(ReleasePush(true, true, 5, 6, true), "V2-a: on, applies, L6, in the start zone: push");
        Require(!ReleasePush(false, true, 5, 6, true) && !ReleasePush(true, false, 5, 6, true), "V2-a: switch off or not selected");
        Require(!ReleasePush(true, true, 5, 6, false), "V2-a: already out of the start zone: no push");
        Require(PushedPriorFailures(true, 0) == 1 && PushedPriorFailures(true, -1) == 1, "V2-a: the push makes the first empty offer due");
        Require(PushedPriorFailures(true, 3) == 3 && PushedPriorFailures(false, 0) == 0, "V2-a: no lowering, no push without it");

        Require(LineDue(0, 5) && LineDue(1000, 1000) && !LineDue(1000, 999), "lines: once per window");
        Require(GateLineSeconds == 600 && FollowLeaveSeconds == 300 && ReleasePushLevels == 3, "constants of the design note");
    }

    std::cout << "zone_keep_policy_tests: all passed\n";
    return 0;
}
