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

TEST(DcRunRobust, StayDeadOnlyInADungeonWithAnActiveRunOrATestParty)
{
    using DcRunRobust::StayDead;
    EXPECT_TRUE(StayDead(true, true, true, false, true, 30, 600));     // a dead member of an active run
    EXPECT_TRUE(StayDead(true, true, false, true, true, 30, 600));     // a .dc test party after the run flag was cleared
    EXPECT_FALSE(StayDead(false, true, true, true, true, 30, 600));    // key off: the stock release as before
    EXPECT_FALSE(StayDead(true, false, true, true, true, 30, 600));    // open world: stock
    EXPECT_FALSE(StayDead(true, true, false, false, true, 30, 600));   // a dungeon without a run (a player's group): stock
}

TEST(DcRunRobust, StayDeadIsBoundedByALivingRezzerAndTheWaitCap)
{
    using DcRunRobust::StayDead;
    // Pre-review 26.10.: a wipe nobody alive can disable must not leave the corpses vetoed with no end.
    EXPECT_FALSE(StayDead(true, true, true, true, false, 30, 600));    // wipe / no living rez class: release
    EXPECT_TRUE(StayDead(true, true, true, false, true, 0, 600));      // right at the death
    EXPECT_TRUE(StayDead(true, true, true, false, true, 599, 600));    // last second of the wait
    EXPECT_FALSE(StayDead(true, true, true, false, true, 600, 600));   // the cap: release
    EXPECT_FALSE(StayDead(true, true, true, true, true, -1, 600));     // unknown death time (logged in dead): release
}

TEST(DcRunRobust, GeoFenceRegroupRetriesEvery30sAndGivesUpAfter5Failures)
{
    using namespace DcRunRobust;
    RegroupState s;
    EXPECT_TRUE(RegroupMayTry(s, 1000));                       // first attempt at once
    EXPECT_FALSE(RegroupRecord(s, false, 1000));               // failure 1
    EXPECT_FALSE(RegroupMayTry(s, 1000 + kRegroupRetryMs - 1)); // not every monitor tick
    EXPECT_TRUE(RegroupMayTry(s, 1000 + kRegroupRetryMs));
    std::uint32_t t = 1000;
    for (int i = 2; i < 5; ++i)
        EXPECT_FALSE(RegroupRecord(s, false, t += kRegroupRetryMs)); // failures 2-4
    EXPECT_TRUE(RegroupRecord(s, false, t += kRegroupRetryMs));      // failure 5: the run ends
    RegroupState ok;
    RegroupRecord(ok, false, 0);
    EXPECT_FALSE(RegroupRecord(ok, true, 5000));               // a success resets the streak
    EXPECT_EQ(ok.failures, 0u);
    EXPECT_TRUE(RegroupMayTry(ok, 5001));
    RegroupState wrap;
    RegroupRecord(wrap, false, 0xFFFFF000u);                   // getMSTime wraps
    EXPECT_FALSE(RegroupMayTry(wrap, 0xFFFFF000u + 1000u));
    EXPECT_FALSE(RegroupMayTry(wrap, 0xFFFFF000u + kRegroupRetryMs - 1));  // after the wrap, still early
    EXPECT_TRUE(RegroupMayTry(wrap, 0xFFFFF000u + kRegroupRetryMs));
}
