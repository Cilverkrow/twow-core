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
    Require(line.find(" resume_skipped=0") != std::string::npos, "the resume skip is listed (twow-repo#485)");
    rejects.resumeSkipped = 40;
    Require(rejects.Format().find(" resume_skipped=40") != std::string::npos,
            "candidates a resumed choice skipped are counted (twow-repo#485)");

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

    // Hotfix 8.5: turn-ins before gathering, grey quests dropped.
    Require(GatherYieldsToTurnIn(true, 1) && !GatherYieldsToTurnIn(true, 0), "one finished quest stops gathering");
    Require(!GatherYieldsToTurnIn(false, 8), "a bot with a real player gathers as before");
    // Hotfix 8.7: XP grey level (level 15 -> 9, level 30 -> 22, level 60 -> 47).
    Require(IsGreyQuest(8, 9) && IsGreyQuest(9, 9) && !IsGreyQuest(10, 9), "grey: at or below the grey level");
    Require(DropGreyQuest(true, 8, 9, false, false), "Mazzranache (8) at level 15 is dropped");
    Require(DropGreyQuest(true, 7, 9, false, false), "Rite of Vision (7) at level 15 is dropped");
    Require(!DropGreyQuest(true, 8, 9, true, false), "a finished grey quest is handed in, not dropped");
    Require(!DropGreyQuest(true, 8, 9, false, true), "class quests stay");
    Require(!DropGreyQuest(false, 8, 9, false, false), "only roster bots on their own");
    Require(!DropGreyQuest(true, 10, 9, false, false), "The Hunter's Way (10) at level 15 stays");

    // Hotfix 8.26: rebind the hearthstone at the rescue target, roster bots on their own only.
    Require(RebindAtRescue(true, false, 0, 0), "roster bot: rebind");
    Require(!RebindAtRescue(true, true, 0, 0), "led by a real player: keep the hearthstone");
    Require(!RebindAtRescue(false, false, 0, 1), "not a roster bot: keep the hearthstone");

    // Hotfix 8.29: Blackstone Island flying machine.
    {
        namespace gi = goblin_island;
        Require(gi::ShouldLeaveIsland(5536, 10, 0), "level 10 on the island: fly off");
        Require(gi::ShouldLeaveIsland(5536, 7, gi::LeaveIdleSeconds), "no progress for 15 min: fly off");
        Require(!gi::ShouldLeaveIsland(5536, 7, gi::LeaveIdleSeconds - 1), "still questing on the island: stay");
        Require(!gi::ShouldLeaveIsland(14, 12, 99999), "not on the island: nothing");
        Require(gi::FollowPath(10.0f, 5000.0f) == gi::IslandPath, "follower at the island machine: path 311");
        Require(gi::FollowPath(5000.0f, 12.0f) == gi::ReturnPath, "follower at the port machine: path 322");
        Require(gi::FollowPath(40.0f, 5000.0f) == 0, "too far from any machine: no flight");
        Require(gi::UsesGoblinStartRescue(9, 9) && !gi::UsesGoblinStartRescue(9, 11) && !gi::UsesGoblinStartRescue(10, 9),
            "goblins below 11 rescued to Durotar only");
        // Hotfix 8.29a: the island box gate (start point and machine inside, Durotar and others outside).
        Require(gi::MayBeOnIsland(9, 1, -233.0f, -7177.0f) && gi::MayBeOnIsland(9, 1, -571.0f, -7850.0f), "goblin on the island: check");
        Require(!gi::MayBeOnIsland(9, 1, 819.0f, -5006.0f) && !gi::MayBeOnIsland(9, 1, 340.0f, -4686.0f), "goblin in Durotar: skip");
        Require(!gi::MayBeOnIsland(2, 1, -233.0f, -7177.0f), "orc: skip");
        Require(!gi::MayBeOnIsland(9, 0, -233.0f, -7177.0f), "other map: skip");
    }

    std::cout << "quest_search_policy_tests passed\n";
    return 0;
}
