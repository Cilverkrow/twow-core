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

    // 8) P1-n (OB-00 26.10., M1 FUTILE): the near trainer trip - no quest gap, an unlearned phase-one skill, within the radius.
    {
        Require(NearTripSkill(kHerbalism, false) && NearTripSkill(kFirstAid, false), "an unlearned phase-one skill: near trip");
        Require(!NearTripSkill(kHerbalism, true), "learned once: the next rank is not a near trip");
        Require(!NearTripSkill(185, false) && !NearTripSkill(171, false), "cooking / alchemy: no phase-one near trip");
        Require(WithinNearRadius(0.0f, 300) && WithinNearRadius(300.0f, 300), "within 300 yd, the edge included");
        Require(!WithinNearRadius(300.5f, 300), "beyond the radius");
        Require(!WithinNearRadius(10.0f, 0), "radius 0: off");
        Require(!WithinNearRadius(-1.0f, 300), "no distance: never");
        Require(NearHoldUntil(1000) == 1000 + kNearHoldSeconds && kNearHoldSeconds == 600, "the near trip holds 10 min");
        Require(NearHoldUntil(1000) - 1000 < kTripHoldSeconds, "shorter than the gap trip's cap");
        Require(NearCheckDue(1000, 0) && NearCheckDue(1060, 1000) && !NearCheckDue(1059, 1000), "one near check per 60 s");
        Require(ClampNearRadius(0) == 0 && ClampNearRadius(-5) == 0, "0 or less: off");
        Require(ClampNearRadius(300) == 300 && ClampNearRadius(5000) == kMaxNearRadius, "at most 1000 yd");
        Require(std::strcmp(kNearCheckValue, kNearTraceValue) != 0 && std::strcmp(kNearCheckValue, kTrainerTripValue) != 0,
            "separate near values; the hourly limit is the trainer trip's");

        // The gate: all passed -> start; otherwise the first gate that refused (the trace's result=).
        Require(NearGateReason(true, true, false, false, true, true, true, true, true) == nullptr, "all gates passed: start");
        Require(std::strcmp(NearGateReason(false, false, true, true, false, false, false, false, false), "not_eligible") == 0, "not eligible first");
        Require(std::strcmp(NearGateReason(true, false, true, false, true, true, true, true, true), "cooldown") == 0, "the hourly limit");
        Require(std::strcmp(NearGateReason(true, true, true, false, true, true, true, true, true), "turn_in") == 0, "an open turn-in goes first");
        Require(std::strcmp(NearGateReason(true, true, false, true, true, true, true, true, true), "band") == 0, "a band walk goes first");
        Require(std::strcmp(NearGateReason(true, true, false, false, false, true, true, true, true), "money") == 0, "money");
        Require(std::strcmp(NearGateReason(true, true, false, false, true, false, false, false, true), "no_trainer") == 0, "nothing to learn");
        Require(std::strcmp(NearGateReason(true, true, false, false, true, true, false, false, true), "not_near") == 0, "no trainer within the radius");
        Require(std::strcmp(NearGateReason(true, true, false, false, true, true, true, false, false), "unreachable") == 0, "near but hostile / unreachable");
    }

    // 9) P1-t (M1r FUTILE 26.10.): the rank filter on the teaching spell. Values from tw_world spell_template / npc_trainer
    // (test DB C): the trainer spell teaches via LEARN_SPELL (36), its own effect 2 is SKILL_STEP (44); only the taught
    // spell has effect 2 SKILL (118), which IsProfessionSpell asks.
    {
        auto const isProfessionSpell = [](std::uint32_t spell)
        {
            return spell == 2366 || spell == 2575 || spell == 8613 || spell == 3273;   // Herb Gathering, Mining, Skinning, First Aid
        };
        Require(TeachesProfessionRank(2372, {36, 44, 0}, {2366, 0, 0}, isProfessionSpell), "Apprentice Herbalist teaches Herb Gathering");
        Require(TeachesProfessionRank(2581, {36, 44, 0}, {2575, 0, 0}, isProfessionSpell), "Apprentice Miner teaches Mining");
        Require(TeachesProfessionRank(8615, {36, 44, 0}, {8613, 0, 0}, isProfessionSpell), "Apprentice Skinning teaches Skinning");
        Require(TeachesProfessionRank(3279, {36, 44, 0}, {3273, 0, 0}, isProfessionSpell), "Apprentice First Aid teaches First Aid");
        Require(!isProfessionSpell(2581) && !isProfessionSpell(2372), "the old filter: the teaching spell itself is no profession spell");
        Require(TeachesProfessionRank(2575, {33, 118, 0}, {0, 0, 0}, isProfessionSpell), "the taught spell itself (cmangos learnedSpell)");
        Require(!TeachesProfessionRank(2658, {36, 0, 0}, {2657, 0, 0}, isProfessionSpell), "a recipe (Smelt Copper) teaches no rank");
        Require(!TeachesProfessionRank(2581, {44, 36, 0}, {2575, 0, 0}, isProfessionSpell), "a trigger counts only on a LEARN_SPELL effect");
        Require(TeachesProfessionRank(2581, {0, 0, 36}, {0, 0, 2575}, isProfessionSpell), "any of the three effects");
        Require(!TeachesProfessionRank(0, {36, 0, 0}, {0, 0, 0}, isProfessionSpell), "LEARN_SPELL without a trigger: nothing");
    }

    // 10) P1-u (owner 26.10. via OB-00, X = 10): below level 10 no profession trip out of the zone the bot stands in.
    // Zone ids (AreaTable): 12 Elwynn Forest (Northshire and Goldshire are sub-areas of it), 1519 Stormwind City, 14 Durotar.
    {
        Require(kHomeZoneBelowLevel == 10, "X = 10 (OB-00 go)");
        Require(HomeZoneOnly(5) && HomeZoneOnly(9), "level 5-9: home zone only");
        Require(!HomeZoneOnly(10) && !HomeZoneOnly(30), "from level 10 on: as before");
        Require(SpawnInHomeZone(12, {12}), "a Goldshire trainer for a Northshire bot: same zone");
        Require(SpawnInHomeZone(12, {1519, 12}), "one spawn in the zone is enough");
        Require(!SpawnInHomeZone(12, {1519}), "Stormwind City is not Elwynn Forest");
        Require(!SpawnInHomeZone(14, {12, 1519}), "another zone");
        Require(!SpawnInHomeZone(12, {}), "no spawn: no trip");
        Require(!SpawnInHomeZone(0, {0}), "unknown zone (0) never counts");
        Require(std::strcmp(NearGateReason(true, true, false, false, true, true, true, true, false), "home_zone") == 0,
            "near, reachable, but outside the zone: home_zone");
        Require(std::strcmp(NearGateReason(true, true, false, false, true, true, true, false, false), "unreachable") == 0,
            "unreachable goes before home_zone");
    }

    std::cout << "profession_travel_policy_tests: OK\n";
    return 0;
}
