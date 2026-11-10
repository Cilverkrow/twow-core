// A3 step 6 (ADR-0041, OB-00 06.11.2026; AiPlayerbot.BotGroups.Manager): the pure rules of the group manager scaffold
// (GroupManagerPolicy.h). The header names no game type, so this suite compiles it standalone. The game side (default 0, the
// one world-thread call site, no group writes in the manager) is pinned by
// t/contracts.d/a3s6_group_manager_scaffold_source_contract_tests.cmake.

#include "GroupManagerPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

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

using namespace ai::group_mgr;

void TestMode()
{
    Require(ValidMode(0) && ValidMode(1) && ValidMode(2) && ValidMode(3), "modes 0..3 are valid");
    Require(!ValidMode(4) && !ValidMode(99), "mode 4+ is not valid");
    Require(SanitizeMode(7) == 0, "an unknown mode is off");
    Require(!Applies(0, 1) && !Applies(0, 2), "mode 0 applies to nobody");
    Require(Applies(1, 1) && Applies(1, 2), "mode 1 applies to everybody");
    Require(Applies(2, 3) && !Applies(2, 4), "mode 2 = cohort B (odd low guid)");
    Require(Applies(3, 4) && !Applies(3, 3), "mode 3 = cohort A (even low guid)");
    Require(!Applies(5, 1), "an unknown mode applies to nobody");
    // T30-1 (BotGroups.Manager.Types, F1 (a), F9 (a)).
    Require(kTypeAdhoc == 1 && kTypeQuest == 2 && kTypesActive == 3, "Types: bit 1 ADHOC, bit 2 QUEST");
    Require(TypesValid(0) && TypesValid(3) && !TypesValid(4) && !TypesValid(7), "reserved bits are not valid");
    Require(SanitizeTypes(7) == 3 && SanitizeTypes(4) == 0, "reserved bits are masked");
    Require(EffectiveTypes(3, 0) == 0 && EffectiveTypes(3, 10) == 3 && EffectiveTypes(6, 10) == 2, "StatusSeconds 0: Types acts as 0");
    Require(TypeOn(1, GroupType::Adhoc) && !TypeOn(1, GroupType::Quest) && TypeOn(2, GroupType::Quest), "one type per bit");
    Require(!TypeOn(7, GroupType::Dungeon), "no DUNGEON type in T30-1");
    Require(!Owns(1, 0, 1) && !Owns(1, 0, 2), "Types 0: the manager owns nobody (the default)");
    Require(Owns(1, 3, 1) && Owns(1, 1, 2), "mode 1 and a type: every bot");
    Require(Owns(2, 2, 3) && !Owns(2, 2, 4), "mode 2: cohort B only");
    Require(!Owns(0, 3, 1) && !Owns(2, 4, 3), "mode 0 or reserved bits only: nobody");
}

// T30-1c: the manager acts only while the arbiter enforces (2); off (0) and shadow (1) make Types act like 0.
void TestLiveTypes()
{
    Require(kManagerArbiter == 2, "the manager needs arbiter 2 (enforce)");
    Require(LiveTypes(3, 10, 2) == 3 && LiveTypes(6, 10, 2) == 2, "arbiter 2: Types after the StatusSeconds rule");
    Require(LiveTypes(3, 10, 0) == 0 && LiveTypes(3, 10, 1) == 0, "arbiter off or shadow: Types acts like 0");
    Require(LiveTypes(3, 0, 2) == 0, "arbiter 2 without StatusSeconds: still 0");
    Require(!Owns(1, LiveTypes(3, 10, 1), 1) && Owns(1, LiveTypes(3, 10, 2), 1), "ownership follows the arbiter");
}

void TestNames()
{
    Require(std::string(TypeName(GroupType::Adhoc)) == "adhoc", "type name adhoc");
    Require(std::string(TypeName(GroupType::Quest)) == "quest", "type name quest");
    Require(std::string(TypeName(GroupType::Dungeon)) == "dungeon", "type name dungeon");
    Require(std::string(RoleName(Role::Healer)) == "healer", "role name healer");
    Require(std::string(LevelFitName(LevelFit::BelowBoss)) == "below_boss", "level fit name");
    Require(std::string(FallbackName(Fallback::HiddenHop)) == "hidden_hop", "fallback name");
}

void TestRole()
{
    RoleRules kitRule;   // planNeedsKit = true (default)
    RoleRules planRule;
    planRule.planNeedsKit = false;

    RoleFacts f;
    Require(ResolveRole(f, kitRule) == Role::Dps, "no facts: dps");

    f.assigned = Role::Healer;
    f.plan = Role::Tank;
    f.kitTank = true;
    Require(ResolveRole(f, kitRule) == Role::Healer, "an assigned role wins over the plan");

    f = RoleFacts();
    f.plan = Role::Tank;
    f.specTank = true;
    f.kitTank = false;   // E1: plan says tank, the live kit cannot tank (TankKitFromLevel below 10/16)
    Require(ResolveRole(f, kitRule) == Role::Dps, "plan tank without a tank kit sits as dps (planNeedsKit)");
    Require(ResolveRole(f, planRule) == Role::Tank, "plan tank counts when the plan alone decides");

    f.kitTank = true;
    Require(ResolveRole(f, kitRule) == Role::Tank, "plan tank with a tank kit is tank");

    f = RoleFacts();
    f.plan = Role::Healer;
    f.kitHeal = false;
    f.specHeal = true;
    Require(ResolveRole(f, kitRule) == Role::Dps, "spec heal without a heal kit is dps");
    f.kitHeal = true;
    Require(ResolveRole(f, kitRule) == Role::Healer, "plan healer with a heal kit is healer");

    f = RoleFacts();
    f.specTank = true;   // feral druid by spec bits; E2 decides kitTank (cat kit: false)
    Require(ResolveRole(f, kitRule) == Role::Dps, "spec tank without the kit is dps");
    f.kitTank = true;    // bear kit
    Require(ResolveRole(f, kitRule) == Role::Tank, "spec tank with the kit is tank");

    f = RoleFacts();
    f.specTank = true;
    f.specHeal = true;
    f.kitTank = true;
    f.kitHeal = true;
    Require(ResolveRole(f, kitRule) == Role::Tank, "tank before healer when both are possible");

    f = RoleFacts();
    f.plan = Role::Dps;
    f.specHeal = true;
    f.kitHeal = true;
    Require(ResolveRole(f, kitRule) == Role::Dps, "a dps plan is always held");
}

void TestComposition()
{
    Role ok[5] = { Role::Dps, Role::Tank, Role::Dps, Role::Healer, Role::Dps };
    Require(DungeonCompositionOk(ok, 5), "1 tank 1 healer 3 dps in any order");

    Role twoTanks[5] = { Role::Tank, Role::Tank, Role::Healer, Role::Dps, Role::Dps };
    Require(!DungeonCompositionOk(twoTanks, 5), "two tanks fail");

    Role noHealer[5] = { Role::Tank, Role::Dps, Role::Dps, Role::Dps, Role::Dps };
    Require(!DungeonCompositionOk(noHealer, 5), "no healer fails");

    Role unknown[5] = { Role::Tank, Role::Healer, Role::Dps, Role::Dps, Role::Unknown };
    Require(!DungeonCompositionOk(unknown, 5), "an unknown role fails");

    Require(!DungeonCompositionOk(ok, 4), "four members fail");

    RoleCount const c = CountRoles(ok, 5);
    Require(c.tank == 1 && c.healer == 1 && c.dps == 3 && c.unknown == 0, "role count");
}

void TestLevelFit()
{
    LevelFitRules r;
    r.bossMargin = 1;
    r.healerSlack = 1;

    MemberLevel g[5] = { { 28, Role::Tank }, { 27, Role::Healer }, { 28, Role::Dps }, { 29, Role::Dps }, { 30, Role::Dps } };
    // bossTop 29, margin 1: floor 28, healer floor 27; max 32
    Require(GroupFitsDungeon(g, 5, 29, 32, r) == LevelFit::Fits, "every member at the floor fits");

    g[2].level = 27;
    Require(GroupFitsDungeon(g, 5, 29, 32, r) == LevelFit::BelowBoss, "a dps one under the floor fails");
    g[2].level = 28;

    g[1].level = 26;
    Require(GroupFitsDungeon(g, 5, 29, 32, r) == LevelFit::BelowBoss, "a healer under the healer floor fails");
    g[1].level = 27;

    g[4].level = 33;
    Require(GroupFitsDungeon(g, 5, 29, 32, r) == LevelFit::AboveMax, "a member above the dungeon max fails");
    g[4].level = 32;
    Require(GroupFitsDungeon(g, 5, 29, 32, r) == LevelFit::Fits, "the dungeon max itself fits");

    Require(GroupFitsDungeon(g, 5, 0, 32, r) == LevelFit::NoData, "no boss level: fail closed");
    Require(GroupFitsDungeon(g, 5, 29, 0, r) == LevelFit::NoData, "no dungeon max: fail closed");
    Require(GroupFitsDungeon(g, 0, 29, 32, r) == LevelFit::Empty, "no members");

    // G1rc lesson: Stockade's strongest boss far above a level-24 group.
    MemberLevel low[1] = { { 24, Role::Dps } };
    LevelFitRules zero;
    Require(GroupFitsDungeon(low, 1, 29, 32, zero) == LevelFit::BelowBoss, "level 24 against a level 29 boss fails");

    Require(FloorFor(2, 5) == 0, "the floor never wraps below 0");
    LevelFitRules big;
    big.bossMargin = 40;
    big.healerSlack = 40;
    MemberLevel one[1] = { { 1, Role::Healer } };
    Require(GroupFitsDungeon(one, 1, 29, 32, big) == LevelFit::Fits, "a margin above the boss level gives floor 0");
}

void TestArrival()
{
    GatherPoint g;
    g.mapId = 36;
    g.instanceId = 7;
    g.x = 100.0f;
    g.y = 100.0f;
    g.z = 10.0f;
    g.radius = 10.0f;
    g.zBand = 5.0f;

    Place p;
    p.alive = true;
    p.inWorld = true;
    p.mapId = 36;
    p.instanceId = 7;
    p.x = 106.0f;
    p.y = 108.0f;   // 2D distance 10: on the edge
    p.z = 12.0f;
    Require(Arrived(p, g), "inside radius and z band, same map and instance");

    Place q = p;
    q.instanceId = 8;
    Require(!Arrived(q, g), "another instance of the same map is not there");
    q = p;
    q.mapId = 0;
    Require(!Arrived(q, g), "another map is not there");
    q = p;
    q.alive = false;
    Require(!Arrived(q, g), "a dead member is not there");
    q = p;
    q.teleporting = true;
    Require(!Arrived(q, g), "a teleporting member is not there");
    q = p;
    q.inWorld = false;
    Require(!Arrived(q, g), "a member not in the world is not there");
    q = p;
    q.x = 107.0f;
    Require(!Arrived(q, g), "outside the radius");
    q = p;
    q.z = 4.0f;
    Require(!Arrived(q, g), "below the z band");
    q.z = 15.0f;
    Require(Arrived(q, g), "the z band edge counts");
    q.z = 15.5f;
    Require(!Arrived(q, g), "above the z band");

    Require(!LegExpired(1000, g), "no deadline never expires");
    g.deadlineMs = 5000;
    Require(!LegExpired(4999, g), "before the deadline");
    Require(LegExpired(5000, g), "at the deadline");
}

void TestFallback()
{
    // observed bot, no teleport allowed (E4 recommendation): walk -> hearthstone -> dissolve
    Require(NextFallback(Fallback::Walk, false, false) == Fallback::Hearthstone, "observed: walk then hearthstone");
    Require(NextFallback(Fallback::Hearthstone, false, false) == Fallback::Dissolve, "hearthstone then dissolve");
    Require(NextFallback(Fallback::Dissolve, true, true) == Fallback::Dissolve, "dissolve is terminal");

    // unobserved, no teleport: walk -> hidden hop -> hearthstone
    Require(NextFallback(Fallback::Walk, true, false) == Fallback::HiddenHop, "unobserved: hidden hop first");
    Require(NextFallback(Fallback::HiddenHop, true, false) == Fallback::Hearthstone, "no teleport: hop then hearthstone");

    // unobserved, teleport allowed: walk -> hidden hop -> teleport -> hearthstone
    Require(NextFallback(Fallback::HiddenHop, true, true) == Fallback::Teleport, "teleport only when allowed");
    Require(NextFallback(Fallback::Teleport, true, true) == Fallback::Hearthstone, "teleport then hearthstone");

    // observed with teleport allowed: never a teleport in sight of a player
    Require(NextFallback(Fallback::Walk, false, true) == Fallback::Hearthstone, "observed: no hop, no teleport");
    Require(NextFallback(Fallback::HiddenHop, false, true) == Fallback::Hearthstone, "observed after a hop: no teleport");
}
}

int main()
{
    TestMode();
    TestLiveTypes();
    TestNames();
    TestRole();
    TestComposition();
    TestLevelFit();
    TestArrival();
    TestFallback();
    std::cout << "group_manager_policy_tests: OK\n";
    return 0;
}
