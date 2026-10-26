// twow-repo#333 Berufe P1-a / P1-b (ProfessionTravelPolicy.h): gap-filler trips of a roster bot to the trainer of a
// phase-one profession and to the vendor of its missing gathering tool.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "ProfessionTravelPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
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
    using namespace ai::profession_travel;

    // 1) Phase one: herbalism, mining, skinning, first aid - no cooking, fishing, survival, crafts.
    {
        Require(IsPhaseOneSkill(182) && IsPhaseOneSkill(186) && IsPhaseOneSkill(393) && IsPhaseOneSkill(129), "phase-one skills");
        Require(!IsPhaseOneSkill(185) && !IsPhaseOneSkill(356) && !IsPhaseOneSkill(142), "no secondary trips but first aid");
        Require(!IsPhaseOneSkill(171) && !IsPhaseOneSkill(164) && !IsPhaseOneSkill(0), "no craft trips in phase one");
    }

    // 2) The quest-first gap: backoff running, no open turn-in, no band walk.
    {
        Require(InQuestGap(100, 200, false, 0), "gap while the backoff runs");
        Require(!InQuestGap(200, 200, false, 0), "backoff ended: no gap (the quest request acts)");
        Require(!InQuestGap(100, 0, false, 0), "no backoff: no gap");
        Require(!InQuestGap(100, 200, true, 0), "an open turn-in comes first");
        Require(!InQuestGap(100, 200, false, 150), "a band walk / band trip comes first");
    }

    // 3) Hold (backoff end + 10 min, at most 30 min), quest-time cost, end reasons, hourly limit.
    {
        Require(HoldUntil(1000, 1240) == 1240 + kHoldAfterBackoffSeconds, "a 4-minute backoff: hold to its end + 10 min");
        Require(HoldUntil(1000, 1000 + 3600) == 1000 + kTripHoldSeconds, "never longer than 30 min");
        Require(kHoldAfterBackoffSeconds == 600 && kTripHoldSeconds == 1800, "CLI-Deep's 10 min, hard cap 30");
        Require(!TripHolds(1000, 0), "never requested: no hold");
        Require(TripHolds(1000, 1840) && TripHolds(1839, 1840), "holds before its end");
        Require(!TripHolds(1840, 1840), "the hold ends");
        Require(HoldOverSeconds(1200, 1240) == 0 && HoldOverSeconds(1300, 1240) == 60, "seconds after the backoff end");
        // P1-b3 (CLI-Deep R-f, cr2-C bot 3125): a trip held for 80 s, its lost line came two hours later.
        Require(LostEndAt(1000, 1080) == 1080, "a lost trip ends at its last hold check that held");
        Require(LostEndAt(1000, 0) == 1000 && LostEndAt(1000, 900) == 1000, "no hold check since this start: it ends at the start");
        Require(HoldOverSeconds(LostEndAt(1000, 1080), 1035) == 45, "hold_over_s of a lost trip counts to its end, not to the line");
        Require(std::strcmp(EndReason(false, true, true), "done") == 0, "reason gone comes first");
        Require(std::strcmp(EndReason(true, true, true), "turn_in") == 0, "an open turn-in ends the trip");
        Require(std::strcmp(EndReason(true, false, false), "cap") == 0, "the hold ran out");
        Require(EndReason(true, false, true) == nullptr, "still holding");
        Require(CooldownOver(5, 0), "never requested: may start");
        Require(!CooldownOver(1000 + kTripCooldownSeconds - 1, 1000), "one trip per hour");
        Require(CooldownOver(1000 + kTripCooldownSeconds, 1000), "the next hour may start again");
        Require(kTripHoldSeconds < kTripCooldownSeconds, "no new request while a trip holds");
    }

    // 4) The cheap gate: mode applied, roster bot on its own, level 5+, hourly limit.
    {
        Require(MayStartTrip(true, true, 5, true), "level 5 roster bot may start");
        Require(!MayStartTrip(false, true, 30, true), "switch off or other cohort: never");
        Require(!MayStartTrip(true, false, 30, true), "not a roster bot on its own: never");
        Require(!MayStartTrip(true, true, kMinLevel - 1, true), "below the apprentice level: never");
        Require(!MayStartTrip(true, true, 30, false), "hourly limit");
    }

    // 5) Missing tool: pick for mining, knife for skinning, mining first, herbalism needs none.
    {
        Require(MissingTool(true, false, true, false) == kMiningPick, "mining pick first");
        Require(MissingTool(true, true, true, false) == kSkinningKnife, "then the knife");
        Require(MissingTool(false, false, false, false) == 0, "no gathering skill: nothing missing");
        Require(MissingTool(true, true, true, true) == 0, "both tools there");
        Require(kMiningPick == 2901 && kSkinningKnife == 7005, "the item ids the loot check asks for");
        Require(HasGatherTool(kMining, true, false) && !HasGatherTool(kMining, false, true), "mining needs the pick");
        Require(HasGatherTool(kSkinning, false, true) && !HasGatherTool(kSkinning, true, false), "skinning needs the knife");
        Require(HasGatherTool(kHerbalism, false, false) && HasGatherTool(356, false, false), "herbalism and fishing: no tool here");
    }

    // 6) The two value keys differ (two kinds, two limits).
    {
        Require(std::strcmp(kTrainerTripValue, kToolTripValue) != 0, "separate trip values");
    }

    // 7) P1-b2 (CLI-PATH 25.10.): a spawn on an open-world map with a travel node within 500 yd; hostile entries first.
    {
        Require(SpawnReachable(true, 0.0f) && SpawnReachable(true, kMaxNodeYards), "a node within 500 yd: reachable");
        Require(!SpawnReachable(true, kMaxNodeYards + 16.0f), "Moonwhisper Coast (nearest node 516 yd): unreachable");
        Require(!SpawnReachable(false, 0.0f), "an instance / battleground map (30, 42, 90): never");
        Require(kMaxNodeYards == 500.0f, "CLI-PATH's rule");
        Require(ClassifyTripTarget(false, true) == TripTarget::Kept, "friendly and reachable: kept");
        Require(ClassifyTripTarget(true, true) == TripTarget::Hostile, "hostile: dropped");
        Require(ClassifyTripTarget(true, false) == TripTarget::Hostile, "hostile counts first");
        Require(ClassifyTripTarget(false, false) == TripTarget::Unreachable, "no reachable spawn: dropped");
        Require(ReachCacheScope(true) == ReachCache::EveryAsker, "the node verdict on its destinations holds for every asker");
        Require(ReachCacheScope(false) == ReachCache::AskerMap, "no destination in this call: only for the asker's map");
    }

    std::cout << "profession_travel_policy_tests: OK\n";
    return 0;
}
