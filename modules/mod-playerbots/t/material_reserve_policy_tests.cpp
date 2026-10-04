#include "MaterialReservePolicy.h"

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
    using namespace ai::material_reserve;
    Config const config;   // 75 / 20

    // Wool rule (owner): tailoring first up to wool, then linen bandages.
    Require(UsableForRecipe(LinenCloth, 30, SkillFirstAid, 0, true, 23, config).usable == 0, "tailoring 23: no linen for first aid");
    Require(UsableForRecipe(LinenCloth, 30, SkillFirstAid, 0, true, 23, config).reason == Reason::WoolRule, "wool rule reason");
    Require(UsableForRecipe(LinenCloth, 30, 197, 0, true, 23, config).usable == 30, "tailoring itself uses all linen");
    Require(UsableForRecipe(LinenCloth, 30, SkillFirstAid, 0, true, 80, config).usable == 10, "tailoring 80: first aid above 20");
    Require(UsableForRecipe(LinenCloth, 15, SkillFirstAid, 0, true, 80, config).usable == 0, "under the reserve: nothing");
    Require(UsableForRecipe(LinenCloth, 30, SkillFirstAid, 0, false, 0, config).usable == 30, "no tailoring: first aid uses all");
    Require(UsableForRecipe(LinenCloth, 30, SkillFirstAid, 25, true, 80, config).usable == 5, "main need above the reserve wins");

    // Main before secondary.
    Require(UsableForRecipe(2672, 15, SkillCooking, 10, false, 0, config).usable == 5, "cooking leaves the main need");
    Require(UsableForRecipe(2672, 15, SkillCooking, 10, false, 0, config).reason == Reason::MainFirst, "main first reason");
    Require(UsableForRecipe(2672, 15, 165, 10, false, 0, config).usable == 15, "main profession uses everything");
    Config off; off.woolTierSkill = 0; off.firstAidClothReserve = 0;
    Require(UsableForRecipe(LinenCloth, 30, SkillFirstAid, 0, true, 23, off).usable == 30, "0 / 0: no wool rule");

    // Memory.
    Memory memory;
    Remember(memory, 2589, 2, 197);   // bolt of linen: main
    Remember(memory, 2589, 1, SkillFirstAid);
    Require(memory[2589].needed && memory[2589].mainNeed == 2 * MainNeedCasts, "main need only from the main profession");
    Require(GreenOrBetter(23, 75, 50) && !GreenOrBetter(50, 75, 50) && !GreenOrBetter(75, 75, 100), "green or better below grey and the rank cap");

    // Selling.
    Require(!MaySell(true, 0.5f, 1) && !MaySell(true, 1.5f, 1), "needed: kept below two stacks");
    Require(MaySell(true, 2.0f, 1), "two stacks: sold as before");
    Require(MaySell(false, 0.5f, 1), "not needed: old rules");

    std::cout << "material_reserve_policy_tests passed\n";
    return 0;
}
