// twow-repo#602 ROT-01 tank count (TankKitPolicy.h, OB-00 28.10.2026, Rotation.TankKitFromLevel default 0): the tank kit only from the level
// of the tank spells - warrior below 10 and protection paladin below 16 get their DPS kit with the switch.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "TankKitPolicy.h"

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
    using namespace ai::tank_kit;

    // 1) The class ids and key levels (vanilla 1.12: Defensive Stance / Taunt / Sunder 10, Righteous Fury 16).
    Require(kClassWarrior == 1 && kClassPaladin == 2, "CLASS_WARRIOR 1, CLASS_PALADIN 2");
    Require(kWarriorTankLevel == 10 && kPaladinTankLevel == 16, "key levels 10 / 16");

    // 2) Modes: 0 off, 1 every bot, 2 cohort B (odd low guid); unknown values never reach the policy (PlayerbotAIConfig maps them to 0).
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");

    // 3) Warrior: below 10 too early (live540: 47 of 90 warriors), from 10 the tank kit.
    Require(TooEarly(true, kClassWarrior, 1) && TooEarly(true, kClassWarrior, 9), "warrior L1 / L9: DPS kit");
    Require(!TooEarly(true, kClassWarrior, 10) && !TooEarly(true, kClassWarrior, 60), "warrior L10 / L60: tank kit");

    // 4) Paladin: below 16 too early, from 16 the tank kit.
    Require(TooEarly(true, kClassPaladin, 4) && TooEarly(true, kClassPaladin, 15), "paladin L4 / L15: DPS kit");
    Require(!TooEarly(true, kClassPaladin, 16) && !TooEarly(true, kClassPaladin, 40), "paladin L16 / L40: tank kit");

    // 5) Other classes are not touched (druid below 10 already has the leveling kit), and the switch off never demotes.
    Require(!TooEarly(true, 11, 5) && !TooEarly(true, 4, 5) && !TooEarly(true, 7, 5), "druid / rogue / shaman: untouched");
    Require(!TooEarly(false, kClassWarrior, 5) && !TooEarly(false, kClassPaladin, 5), "switch off: the old tab decides");

    std::cout << "tank_kit_policy_tests passed\n";
    return 0;
}
