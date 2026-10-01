#include "QuestSearchPolicy.h"

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
    using namespace ai::quest_search;

    // A) the rejection line names every reason.
    RejectCounts rejects;
    rejects.notActive = 7;
    rejects.zoneLevel = 2;
    std::string const line = rejects.Format();
    Require(line.find("not_active=7") != std::string::npos, "not_active counted");
    Require(line.find("zone_level=2") != std::string::npos, "zone_level counted");
    Require(line.find("moved_away=0") != std::string::npos, "all reasons listed");

    // B) the search widens in steps, at most once per two minutes.
    Require(GiverRadius(1, 0) == 2000.f, "stage 0: today's radius (level 1)");
    Require(GiverRadius(40, 0) == 2000.f, "stage 0: floor 2000");
    Require(GiverRadius(1, 1) == 6000.f, "stage 1: neighbouring zones");
    Require(GiverRadius(1, 2) == 12000.f, "stage 2: region");
    Require(GiverRadius(1, 9) == 12000.f, "no stage beyond 2");
    Require(NextStage(0, 0, 1000) == 1, "first widening");
    Require(NextStage(1, 1000, 1119) == 1, "not twice within two minutes");
    Require(NextStage(1, 1000, 1120) == 2, "second widening after two minutes");
    Require(NextStage(2, 0, 5000) == 2, "capped at the region");

    // Owner addition: a zone clearly below the bot, grey targets.
    Require(ZoneBelowBot(5, 14), "Dun Morogh (5) for a level 14 bot: too low");
    Require(!ZoneBelowBot(11, 14), "three levels below is still fine");
    Require(!ZoneBelowBot(0, 14), "unknown zone level: no decision");
    Require(SkipGreyTarget(true, true, false, false), "grey, not needed, not attacking: skip");
    Require(!SkipGreyTarget(true, true, true, false), "a quest needs it: keep");
    Require(!SkipGreyTarget(true, true, false, true), "it attacks the bot: fight back");
    Require(!SkipGreyTarget(false, true, false, false), "bots led by a player: unchanged");
    GreyEngagements grey;
    grey.Add(1000);
    grey.Add(1500);
    Require(!grey.Due(4599), "report once per hour");
    Require(grey.Due(4600) && grey.count == 2, "two grey engagements in the hour");
    grey.Reset();
    Require(!grey.Due(99999), "nothing to report after reset");

    // C) progress and the rescue teleport.
    ProgressTracker progress;
    Require(progress.Update(42, 100), "the first sample counts as progress");
    Require(!progress.Update(42, 160), "same snapshot: no progress");
    Require(progress.IdleSeconds(1300) == 1200, "idle since the last change");
    Require(progress.Update(43, 1300), "a quest state change is progress");
    Require(progress.IdleSeconds(1300) == 0, "idle resets on progress");

    Require(!RescueDue(1799, 2, 0, 5000), "not before 30 minutes (default)");
    Require(!RescueDue(1800, 1, 0, 5000), "not before the search reached the region");
    Require(RescueDue(1800, 2, 0, 5000), "30 minutes idle at stage 2: rescue");
    Require(RescueDue(1200, 2, 0, 5000, 1200), "a configured 20 minutes also works");
    Require(!RescueDue(1800, 2, 5000, 5000 + 7199), "one rescue per bot in two hours");
    Require(RescueDue(1800, 2, 5000, 5000 + 7200), "again after two hours");

    RescueLimiter limiter;
    for (uint32_t i = 0; i < RescueGlobalMax; ++i)
        Require(limiter.TryAcquire(1000 + i), "up to five rescues in ten minutes");
    Require(!limiter.TryAcquire(1100), "the sixth waits");
    Require(limiter.TryAcquire(1000 + 600), "a slot frees after ten minutes");

    Require(!UsesRescueAnchors(10, 5), "high elf level <= 10: starting area");
    Require(UsesRescueAnchors(10, 15), "high elf level 11-20: Auberdine hub");
    Require(UsesRescueAnchors(9, 12), "goblin level 11-20: Ratchet");
    Require(!UsesRescueAnchors(1, 15), "human level 11-20: inn cache");
    Require(!UsesRescueAnchors(9, 30), "above 20: inn cache");
    Require(RescueAnchorEntries(10).size() == 2 && RescueAnchorEntries(9).size() == 1, "anchors: Auberdine, Ratchet");
    Require(RescueAnchorEntries(1).empty(), "no anchors for the other races");

    // Hotfix 8.1: rescue without route requests, [Idle] cadence, route backoff, stale bots.
    Require(!HardIdleRescueDue(3599, 0, 10000, 1800) && HardIdleRescueDue(3600, 0, 10000, 1800),
            "twice the idle limit rescues regardless of the stage");
    Require(!HardIdleRescueDue(7200, 9000, 10000, 1800), "per-bot rescue cooldown still applies");
    Require(!IdleLogDue(599, 0, 1000) && IdleLogDue(600, 0, 1000), "[Idle] after ten minutes");
    Require(!IdleLogDue(5000, 1000, 2799) && IdleLogDue(5000, 1000, 2800), "then every thirty minutes");
    Require(RouteBackoffSeconds(1) == 120 && RouteBackoffSeconds(2) == 240 && RouteBackoffSeconds(3) == 480 &&
            RouteBackoffSeconds(4) == 600 && RouteBackoffSeconds(20) == 600, "backoff 2, 4, 8, 10 minutes");
    Require(!UpdateStale(0, 5000) && !UpdateStale(4500, 5000) && UpdateStale(4400, 5000), "stale after ten minutes");

    std::cout << "quest_search_policy_tests passed\n";
    return 0;
}
