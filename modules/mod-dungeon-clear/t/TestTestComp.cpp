/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestComp.h"

#include <set>
#include <string_view>

using DcTestComp::BuildComp;
using DcTestComp::kPartySize;
using DcTestComp::RolePool;
using DcTestComp::Slot;

namespace
{
    // Death knight (6) is excluded; every other pooled class is one of these.
    bool IsKnownClass(std::uint8_t c)
    {
        return c == 1 || c == 2 || c == 3 || c == 4 || c == 5 ||
               c == 7 || c == 8 || c == 9 || c == 11;
    }
}

// Same seed -> byte-identical comp (classes and specs). This is what lets a
// bug-tripping run be replayed via `.dc test start <d> seed=N`.
TEST(DcTestComp, DeterministicForSameSeed)
{
    for (std::uint32_t seed : {1u, 7u, 42u, 1000u, 0xABCDEF12u})
    {
        auto a = BuildComp(seed);
        auto b = BuildComp(seed);
        for (std::size_t i = 0; i < kPartySize; ++i)
        {
            EXPECT_EQ(a[i].classId, b[i].classId) << "seed=" << seed << " slot=" << i;
            EXPECT_STREQ(a[i].specName, b[i].specName) << "seed=" << seed << " slot=" << i;
        }
    }
}

// Slot 0 is always the tank (leader), slot 1 the healer, slots 2-4 DPS.
TEST(DcTestComp, RoleLayoutIsStable)
{
    for (std::uint32_t seed = 0; seed < 500; ++seed)
    {
        auto c = BuildComp(seed);
        EXPECT_STREQ(c[0].role, "tank") << "seed=" << seed;
        EXPECT_STREQ(c[1].role, "heal") << "seed=" << seed;
        for (std::size_t i = 2; i < kPartySize; ++i)
            EXPECT_STREQ(c[i].role, "dps") << "seed=" << seed << " slot=" << i;
    }
}

// All five bots are on distinct, known (non-DK) classes with a named spec.
TEST(DcTestComp, DistinctKnownClassesWithSpecs)
{
    for (std::uint32_t seed = 0; seed < 500; ++seed)
    {
        auto c = BuildComp(seed);
        std::set<std::uint8_t> classes;
        for (std::size_t i = 0; i < kPartySize; ++i)
        {
            EXPECT_TRUE(IsKnownClass(c[i].classId))
                << "seed=" << seed << " classId=" << int(c[i].classId);
            EXPECT_NE(c[i].classId, 6) << "death knight leaked into comp, seed=" << seed;
            ASSERT_NE(c[i].specName, nullptr);
            EXPECT_NE(std::string_view(c[i].specName), std::string_view())
                << "empty spec, seed=" << seed << " slot=" << i;
            classes.insert(c[i].classId);
        }
        EXPECT_EQ(classes.size(), kPartySize) << "duplicate class, seed=" << seed;
    }
}

// The point of the feature: comps actually vary. Over many seeds we expect
// more than one tank class, more than one healer class, and a healthy spread
// of DPS classes — otherwise the "randomisation" is stuck on one shape.
TEST(DcTestComp, ActuallyVaries)
{
    std::set<std::uint8_t> tanks, heals, dps;
    for (std::uint32_t seed = 0; seed < 500; ++seed)
    {
        auto c = BuildComp(seed);
        tanks.insert(c[0].classId);
        heals.insert(c[1].classId);
        for (std::size_t i = 2; i < kPartySize; ++i)
            dps.insert(c[i].classId);
    }
    EXPECT_GT(tanks.size(), 1u);
    EXPECT_GT(heals.size(), 1u);
    EXPECT_GE(dps.size(), 5u);
}

TEST(DcTestComp, RolePoolLookup)
{
    EXPECT_FALSE(RolePool("tank").empty());
    EXPECT_FALSE(RolePool("heal").empty());
    EXPECT_FALSE(RolePool("dps").empty());
    EXPECT_TRUE(RolePool("bogus").empty());

    // Every pool entry carries the role token it was fetched under.
    for (Slot const& s : RolePool("tank"))
        EXPECT_STREQ(s.role, "tank");
    for (Slot const& s : RolePool("heal"))
        EXPECT_STREQ(s.role, "heal");
    for (Slot const& s : RolePool("dps"))
        EXPECT_STREQ(s.role, "dps");
}

// ---- CLI-GRP G2 (28.10.2026): the forced tank seat (`tank=<class>`) ------------------

TEST(DcTestComp, ForcedTankSeatTakesThatClassForEverySeed)
{
    for (std::uint32_t seed = 1; seed < 200; ++seed)
        for (std::uint8_t tank : { std::uint8_t(1), std::uint8_t(2) })   // warrior, paladin
        {
            auto const comp = BuildComp(seed, tank);
            EXPECT_EQ(comp[0].classId, tank);
            EXPECT_EQ(std::string_view(comp[0].role), "tank");
            std::set<std::uint8_t> classes;
            for (Slot const& s : comp)
                classes.insert(s.classId);
            EXPECT_EQ(classes.size(), kPartySize) << "seed " << seed;   // the other seats exclude the forced class
        }
}

TEST(DcTestComp, ForcingTheDrawnClassChangesNothing)
{
    for (std::uint32_t seed = 1; seed < 200; ++seed)
    {
        auto const drawn = BuildComp(seed);
        auto const forced = BuildComp(seed, drawn[0].classId);
        for (std::size_t i = 0; i < kPartySize; ++i)
            EXPECT_EQ(forced[i].classId, drawn[i].classId) << "seed " << seed << " seat " << i;
        auto const zero = BuildComp(seed, 0);
        for (std::size_t i = 0; i < kPartySize; ++i)
            EXPECT_EQ(zero[i].classId, drawn[i].classId);   // 0 = drawn, as before
    }
}

TEST(DcTestComp, TankClassByNameKnowsOnlyTheTankPool)
{
    EXPECT_EQ(DcTestComp::TankClassByName("warrior"), 1);
    EXPECT_EQ(DcTestComp::TankClassByName("paladin"), 2);
    EXPECT_EQ(DcTestComp::TankClassByName("druid"), 11);   // forced only (kForcedOnlyTankPool), never drawn
    EXPECT_EQ(DcTestComp::TankClassByName("rogue"), 4);    // forced only (kForcedOnlyTankPool), never drawn
    EXPECT_EQ(DcTestComp::TankClassByName("mage"), 0);
    EXPECT_EQ(DcTestComp::TankClassByName(""), 0);
}

// CLI-GRP 29.10.2026 (g2/CAUSE-DRUIDE-FERAL-SITZ): the druid is a forced-only tank seat - tank=druid gives it, the draw
// never does (the seeds of every other campaign keep their comps).
TEST(DcTestComp, DruidTankIsForcedOnlyNeverDrawn)
{
    for (std::uint32_t seed = 1; seed < 500; ++seed)
    {
        EXPECT_NE(BuildComp(seed)[0].classId, 11) << "seed " << seed;   // never drawn
        auto const forced = BuildComp(seed, 11);
        EXPECT_EQ(forced[0].classId, 11);
        EXPECT_EQ(std::string_view(forced[0].specName), "bear");
        EXPECT_EQ(std::string_view(forced[0].role), "tank");
    }
}

// CLI-GRP 30.10.2026 (OB-00, twow-repo#654): the rogue tank (premade path 4.3) is a forced-only seat as well - tank=rogue
// gives it, the draw never does; with the seat forced no other seat is a rogue (distinct classes, the rogue dps drop out).
TEST(DcTestComp, RogueTankIsForcedOnlyNeverDrawn)
{
    for (std::uint32_t seed = 1; seed < 500; ++seed)
    {
        EXPECT_NE(BuildComp(seed)[0].classId, 4) << "seed " << seed;   // never drawn
        auto const forced = BuildComp(seed, 4);
        EXPECT_EQ(forced[0].classId, 4);
        EXPECT_EQ(std::string_view(forced[0].specName), "rogue tank");
        EXPECT_EQ(std::string_view(forced[0].role), "tank");
        for (std::size_t i = 1; i < forced.size(); ++i)
            EXPECT_NE(forced[i].classId, 4) << "seed " << seed << " seat " << i;
    }
}
