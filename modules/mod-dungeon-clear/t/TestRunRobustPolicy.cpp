/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "DcRunRobustPolicy.h"

// CLI-GRP G1 dry-r3 (25.10.2026): the pure rules of the run-robustness keys. Off (all 0) they must be the old behaviour.
using DcRunRobust::ClearDeathLoop;
using DcRunRobust::EvaluateLootYield;
using DcRunRobust::FoughtSince;
using DcRunRobust::LootYieldStep;

namespace
{
constexpr std::uint32_t kTimeout = 15000;
}

TEST(DcRunRobust, DeathLoopClearedOnlyAliveInTheDungeonOfAnActiveRun)
{
    EXPECT_TRUE(ClearDeathLoop(true, true, true, true));
    EXPECT_FALSE(ClearDeathLoop(false, true, true, true));   // key off: the stock death loop as before
    EXPECT_FALSE(ClearDeathLoop(true, false, true, true));   // dead: its deaths still count until it is up again
    EXPECT_FALSE(ClearDeathLoop(true, true, false, true));   // open world: the stock evacuation is the safety net
    EXPECT_FALSE(ClearDeathLoop(true, true, true, false));   // a dungeon without a run (a player's group)
}

TEST(DcRunRobust, LootYieldOffIsTheOldRule)
{
    // first loot tick anchors, yield for the timeout, then give up; no loot resets the anchor
    LootYieldStep s = EvaluateLootYield(true, false, false, 0, false, 1000, kTimeout);
    EXPECT_EQ(s.start, 1000u);
    EXPECT_TRUE(s.anchored);
    EXPECT_TRUE(s.yield);
    s = EvaluateLootYield(true, false, false, 1000, false, 1000 + kTimeout - 1, kTimeout);
    EXPECT_TRUE(s.yield);
    EXPECT_FALSE(s.anchored);
    s = EvaluateLootYield(true, false, false, 1000, false, 1000 + kTimeout, kTimeout);
    EXPECT_TRUE(s.giveUp);
    EXPECT_EQ(s.start, 1000u);   // kept expired: keep following until the flags drop
    s = EvaluateLootYield(false, false, false, 1000, false, 2000, kTimeout);
    EXPECT_EQ(s.start, 0u);
    EXPECT_FALSE(s.yield);
    EXPECT_FALSE(s.giveUp);
    // the party fights, but the gate is off: the old yield
    s = EvaluateLootYield(true, true, false, 0, false, 1000, kTimeout);
    EXPECT_TRUE(s.yield);
    // a fight since the anchor, but the key is off: no re-anchor
    EXPECT_FALSE(FoughtSince(false, 100, 160));
}

TEST(DcRunRobust, AnchorSetBeforeAFightIsSetAnewAfterIt)
{
    // G1 dry-r3: anchored at second 100 (t=1000 ms), fight from second 130, next evaluation 58 s later
    EXPECT_TRUE(FoughtSince(true, 100, 130));
    LootYieldStep s = EvaluateLootYield(true, false, false, 1000, false, 59000, kTimeout);
    EXPECT_TRUE(s.giveUp);   // the old rule: the fresh corpse given up at once
    s = EvaluateLootYield(true, false, false, 1000, true, 59000, kTimeout);
    EXPECT_EQ(s.start, 59000u);
    EXPECT_TRUE(s.anchored);
    EXPECT_TRUE(s.yield);
    // a fight in the anchor's own second does not count (it would re-anchor on every evaluation)
    EXPECT_FALSE(FoughtSince(true, 100, 100));
    EXPECT_FALSE(FoughtSince(true, 0, 130));   // no anchor yet
}

TEST(DcRunRobust, SlowEvaluationsStillGiveUp)
{
    // pre-review 25.10.: a bot walking to a far corpse is evaluated only every ~3 s; without a fight the yield must still
    // end at the timeout (a wall-time gap rule would have re-anchored on every evaluation)
    std::uint32_t start = 0;
    bool gaveUp = false;
    for (std::uint32_t t = 1000; t <= 1000 + 2 * kTimeout && !gaveUp; t += 3000)
    {
        LootYieldStep const s = EvaluateLootYield(true, false, false, start, FoughtSince(true, 100, 99), t, kTimeout);
        start = s.start;
        gaveUp = s.giveUp;
    }
    EXPECT_TRUE(gaveUp);
}

TEST(DcRunRobust, NoYieldWhileThePartyFightsWithTheGate)
{
    LootYieldStep s = EvaluateLootYield(true, true, true, 5000, false, 6000, kTimeout);
    EXPECT_FALSE(s.yield);
    EXPECT_FALSE(s.giveUp);
    EXPECT_EQ(s.start, 0u);   // keep to the tank; the corpse counts anew after the fight
    s = EvaluateLootYield(true, false, true, 0, false, 7000, kTimeout);
    EXPECT_TRUE(s.yield);     // fight over: yield as usual
}
