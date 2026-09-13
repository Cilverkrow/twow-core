#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
bool UsesPolicy(bool enabled, bool rosterMember)
{
    return enabled && rosterMember;
}

bool MayAutonomouslyAccept(bool enabled, bool rosterMember, uint32_t activeSlots, uint32_t softLimit,
    uint32_t botLevel, uint32_t questLevel, uint32_t rejectDelta, uint32_t maxAboveDelta)
{
    if (!UsesPolicy(enabled, rosterMember))
        return true;
    return activeSlots < softLimit && botLevel < questLevel + rejectDelta &&
        questLevel <= botLevel + maxAboveDelta;
}

bool IsLocalActiveQuestHubCandidate(bool sameMap, bool activeQuestDestination, bool normallyActive,
    float distance, float hubRadius)
{
    return sameMap && activeQuestDestination && normallyActive && distance <= hubRadius;
}

bool PreferLocalQuest(bool enabled, bool rosterMember, bool realMaster, bool hasLocalActiveHubCandidate)
{
    return UsesPolicy(enabled, rosterMember) && !realMaster && hasLocalActiveHubCandidate;
}

bool MayAutonomouslyTravelWithMaster(bool enabled, bool rosterMember, bool realMaster, bool gatheringHerbOrMining,
    bool withinGatherLeash, bool explicitMasterRequest)
{
    if (!UsesPolicy(enabled, rosterMember) || !realMaster)
        return true;
    if (gatheringHerbOrMining)
        return withinGatherLeash;
    return explicitMasterRequest;
}

bool MayYieldFollowToRpg(bool enabled, bool rosterMember, bool realMaster)
{
    return !UsesPolicy(enabled, rosterMember) || !realMaster;
}

bool MustCancelGatherForMovingMaster(bool enabled, bool rosterMember, bool realMaster, bool masterMoving,
    bool herbOrMiningTarget)
{
    return UsesPolicy(enabled, rosterMember) && realMaster && masterMoving && herbOrMiningTarget;
}

bool MaySafelyRetire(bool enabled, bool rosterMember, uint32_t botLevel, uint32_t questLevel, uint32_t retireDelta,
    bool completeOrRewarded, bool hasObjectiveProgress, bool itemOrSourceItem, bool chainOrExclusive,
    bool classProfessionTimedOrInstance, bool focusQuest, bool realMaster)
{
    if (!UsesPolicy(enabled, rosterMember) || botLevel < questLevel + retireDelta)
        return false;
    return !completeOrRewarded && !hasObjectiveProgress && !itemOrSourceItem && !chainOrExclusive &&
        !classProfessionTimedOrInstance && !focusQuest && !realMaster;
}
}

int main()
{
    // Default-off and non-roster calls are exact legacy pass-throughs.
    assert(!UsesPolicy(false, true));
    assert(!UsesPolicy(true, false));
    assert(MayAutonomouslyAccept(false, true, 20, 16, 60, 1, 4, 1));
    assert(MayAutonomouslyAccept(true, false, 20, 16, 60, 1, 4, 1));

    // Autonomous offer boundaries: the lower stale-offer boundary, an upper
    // level band, and the unchanged hard limit are independent.
    assert(MayAutonomouslyAccept(true, true, 15, 16, 20, 17, 4, 1));
    assert(!MayAutonomouslyAccept(true, true, 16, 16, 20, 17, 4, 1));
    assert(MayAutonomouslyAccept(true, true, 0, 16, 20, 17, 4, 1)); // delta 3
    assert(!MayAutonomouslyAccept(true, true, 0, 16, 20, 16, 4, 1)); // delta 4
    assert(MayAutonomouslyAccept(true, true, 0, 16, 1, 1, 4, 1));
    assert(MayAutonomouslyAccept(true, true, 0, 16, 1, 2, 4, 1));
    assert(!MayAutonomouslyAccept(true, true, 0, 16, 1, 4, 4, 1));
    assert(!MayAutonomouslyAccept(true, true, 0, 16, 1, 5, 4, 1));

    // A local hub is a normal active quest destination in the same map and
    // bounded radius, not simply any destination on the same map. A remote
    // follow-up hub on that map must not displace the active local objective.
    constexpr float hubRadius = 1200.0f;
    assert(IsLocalActiveQuestHubCandidate(true, true, true, 400.0f, hubRadius));
    assert(!IsLocalActiveQuestHubCandidate(true, true, true, 3500.0f, hubRadius));
    assert(!IsLocalActiveQuestHubCandidate(true, false, true, 400.0f, hubRadius));
    assert(!IsLocalActiveQuestHubCandidate(true, true, false, 400.0f, hubRadius));
    assert(PreferLocalQuest(true, true, false, true));
    assert(!PreferLocalQuest(true, true, false, false));
    assert(!PreferLocalQuest(true, true, true, true));
    assert(MayAutonomouslyTravelWithMaster(true, true, true, true, true, false));
    assert(!MayAutonomouslyTravelWithMaster(true, true, true, true, false, false));
    assert(!MayAutonomouslyTravelWithMaster(true, true, true, false, true, false));
    assert(MayAutonomouslyTravelWithMaster(true, true, true, false, false, true));
    assert(!MayAutonomouslyTravelWithMaster(true, true, true, true, false, true));
    assert(MayYieldFollowToRpg(false, true, true));
    assert(!MayYieldFollowToRpg(true, true, true));
    assert(MustCancelGatherForMovingMaster(true, true, true, true, true));
    assert(!MustCancelGatherForMovingMaster(true, true, true, false, true));
    assert(!MustCancelGatherForMovingMaster(true, true, true, true, false));

    // Retirement is only for a truly unprogressed autonomous ordinary quest.
    assert(MaySafelyRetire(true, true, 20, 14, 6, false, false, false, false, false, false, false));
    assert(!MaySafelyRetire(true, true, 20, 15, 6, false, false, false, false, false, false, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, true, false, false, false, false, false, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, false, true, false, false, false, false, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, false, false, true, false, false, false, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, false, false, false, true, false, false, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, false, false, false, false, true, false, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, false, false, false, false, false, true, false));
    assert(!MaySafelyRetire(true, true, 20, 14, 6, false, false, false, false, false, false, true));

    std::cout << "quest_first_policy=PASS default_off=PASS quest_band=PASS local_stickiness=PASS "
        "master_leash=PASS moving_master_gather_cancel=PASS safe_retire=PASS item_safety=PASS\n";
}
