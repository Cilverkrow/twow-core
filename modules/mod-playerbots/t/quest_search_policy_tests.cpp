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

    // Hotfix 8.32: grind fallback after repeated empty quest route searches.
    Require(!GrindFallback(true, 1, 2) && GrindFallback(true, 2, 2) && GrindFallback(true, 5, 2), "from the 2nd empty search");
    Require(!GrindFallback(true, 9, 0), "0 = off");
    Require(!GrindFallback(false, 9, 2), "not quest-first: unaffected (stock grind rules)");

    // twow-repo#594 / P3-a: fresh bots (DirectTargets) - short backoff, own grind threshold; off = 8.1 / 8.32 unchanged.
    Require(RouteBackoffSeconds(3, false, 20) == 480 && RouteBackoffSeconds(3, true, 0) == 480, "not fresh or 0 s: the 8.1 rule");
    Require(RouteBackoffSeconds(1, true, 20) == 20 && RouteBackoffSeconds(9, true, 20) == 20, "fresh: FreshBackoffSeconds at any count");
    Require(FreshBot(true, 3, 5) && FreshBot(true, 5, 5) && !FreshBot(true, 6, 5), "fresh up to FreshMaxLevel");
    Require(!FreshBot(false, 1, 5) && !FreshBot(true, 1, 0), "master off or max level 0: nobody is fresh");
    {
        RejectCounts counts;
        Require(counts.FormatInactiveReasons().empty() && counts.Format().find("not_active_reasons") == std::string::npos,
            "no reasons counted: the rejected line is unchanged");
        counts.notActive = 2;
        counts.notActiveReason[RejectCounts::InactiveNoMark] = 2;
        Require(counts.Format().find(" not_active=2 ") != std::string::npos &&
            counts.Format().find("not_active_reasons=impossible:0,map:0,no_mark:2,") != std::string::npos,
            "reasons appended, the total stays");
    }
    {
        // twow-repo#601 (QuestLoop.InactiveReselect): the emptied-camp skips only appear when counted.
        RejectCounts counts;
        Require(counts.Format().find("inactive_reselect") == std::string::npos, "no emptied-camp skip: the rejected line is unchanged");
        counts.inactiveReselect = 3;
        counts.notActive = 1;
        counts.notActiveReason[RejectCounts::InactiveMap] = 1;
        std::string const line = counts.Format();
        Require(line.find(" battleground_quest=0 inactive_reselect=3 not_active_reasons=") != std::string::npos,
            "counted after battleground_quest, before the not_active split");
    }

    // twow-repo#601 quest acceptance step 1b: no_mark split by the first failed CanTakeQuest check, in its order.
    {
        QuestTakeChecks all;
        Require(NoMarkOf(all) == NoMarkOther, "every check passes: other");
        QuestTakeChecks c = all;
        c.status = false;
        Require(NoMarkOf(c) == NoMarkInLog, "status fails, not rewarded: in the log");
        c.notRewarded = false;
        Require(NoMarkOf(c) == NoMarkDone, "status fails, rewarded: done");
        c = all;
        c.maxLevel = false;
        c.status = false;
        Require(NoMarkOf(c) == NoMarkMaxLevel, "max level comes first");
        c = all;
        c.prevQuest = false;
        c.level = false;
        Require(NoMarkOf(c) == NoMarkLevel, "level before the previous quest (CanTakeQuest order)");
        c = all;
        c.prevQuest = false;
        Require(NoMarkOf(c) == NoMarkPrevQuest, "previous quest");
        c = all;
        c.chain = false;
        Require(NoMarkOf(c) == NoMarkChain, "chain");
        c = all;
        c.active = false;
        Require(NoMarkOf(c) == NoMarkInactiveQuest, "inactive quest");
        Require(std::string(NoMarkName(NoMarkPrevQuest)) == "prev_quest" && std::string(NoMarkName(NoMarkReasons)) == "other",
            "names, out of range = other");
        Require(std::string(NoMarkName(NoMarkMasked)) == "masked" && std::string(NoMarkName(NoMarkRepeatable)) == "repeatable" &&
            std::string(NoMarkName(NoMarkLowLevelMasked)) == "low_level_masked" && std::string(NoMarkName(NoMarkOther)) == "other",
            "the dialog status split names, other last");

        RejectCounts counts;
        counts.notActiveReason[RejectCounts::InactiveNoMark] = 3;
        Require(counts.FormatInactiveReasons().find("no_mark_why") == std::string::npos, "no split counted: no token");
        counts.noMarkWhy[NoMarkDone] = 2;
        counts.noMarkWhy[NoMarkPrevQuest] = 1;
        Require(counts.FormatInactiveReasons() ==
            " not_active_reasons=impossible:0,map:0,no_mark:3,low_level_no_reward:0,no_turnin:0,hostile:0,other:0 no_mark_why=done:2,prev_quest:1",
            "no_mark_why after the split, only the counted reasons");
    }

    // twow-repo#601 quest acceptance step 1b: the giver drop counters of a traced fetch.
    {
        GiverDropCounts drops;
        Require(drops.Format() == "giver_drops=strategy:0,focus:0,quest_level:0,min_max:0,band_grey:0,condition:0,free_slots:0,elite:0"
            " giver_kept=0 giver_out_of_range=0", "empty counters");
        drops.Drop(GiverDropFreeSlots);
        drops.Drop(GiverDropFreeSlots);
        drops.Drop(GiverDropQuestLevel);
        drops.Drop(GiverDrops);   // out of range: ignored
        drops.kept = 4;
        drops.outOfRange = 310;
        Require(drops.Format() == "giver_drops=strategy:0,focus:0,quest_level:1,min_max:0,band_grey:0,condition:0,free_slots:2,elite:0"
            " giver_kept=4 giver_out_of_range=310", "counted by rule");
    }

    std::cout << "quest_search_policy_tests passed\n";
    return 0;
}
