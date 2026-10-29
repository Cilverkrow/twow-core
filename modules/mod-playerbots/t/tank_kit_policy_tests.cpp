// twow-repo#602 ROT-01 tank count (TankKitPolicy.h, OB-00 28.10.2026, Rotation.TankKitFromLevel default 0): the tank kit only from the level
// of the tank spells - warrior below 10 and protection paladin below 16 get their DPS kit with the switch; path default (OB-00 29.10.):
// a warrior without talent points starts from its path's tab, warriors only.
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

    // 6) The DPS tab a too-early tank gets: warrior arms (0), paladin retribution (2); other classes have none.
    Require(DpsTab(kClassWarrior) == 0 && DpsTab(kClassPaladin) == 2 && DpsTab(11) == -1, "DPS tabs");

    // 7) Level changes that need a strategy rebuild: across the key level, either way; never on the first pass (from level 0).
    Require(CrossesKey(kClassWarrior, 9, 10) && CrossesKey(kClassWarrior, 8, 11), "warrior 9 -> 10, 8 -> 11: rebuild");
    Require(!CrossesKey(kClassWarrior, 10, 11) && !CrossesKey(kClassWarrior, 5, 9), "warrior inside a band: no rebuild");
    Require(CrossesKey(kClassPaladin, 15, 16) && !CrossesKey(kClassPaladin, 9, 10), "paladin 15 -> 16 rebuilds, 9 -> 10 not");
    Require(CrossesKey(kClassWarrior, 12, 3), "a level reset across the key level: rebuild");
    Require(!CrossesKey(kClassWarrior, 0, 12) && !CrossesKey(11, 9, 10), "first pass and other classes: no rebuild");

    // 8) The tank tabs: warrior 2, paladin 1 (an arms warrior reaching 10 is no tank: no rebuild).
    Require(IsTankTab(kClassWarrior, 2) && IsTankTab(kClassPaladin, 1), "warrior 2, paladin 1");
    Require(!IsTankTab(kClassWarrior, 0) && !IsTankTab(kClassPaladin, 2) && !IsTankTab(11, 1), "arms, retribution, druid: no tank tab");

    // 9) Path default (OB-00 29.10.2026): the path's tab is its key, else the main tree of its first link; nothing usable = -1.
    Require(PathKitTab(1, 0) == 1 && PathKitTab(-1, 0) == 0 && PathKitTab(-1, 2) == 2, "key first, else the first link's tree");
    Require(PathKitTab(-1, -1) == -1 && PathKitTab(3, -1) == -1 && PathKitTab(-1, 5) == -1, "no usable tab: -1");

    // 10) The kit base tab per warrior path, without talent points (the live config: tabs from the first links; default tab 2).
    //     arms (0): arms below and at 10 - the fix (it was rebuilt to the tank kit at 10, tank-B 28.10.);
    //     fury (1): fury below and at 10 (below 10 it was demoted to arms before);
    //     protection (2): the tank tab, so it is demoted to arms below 10 (TooEarly) and tanks from 10 - as before;
    //     no path (-1): the default tab 2 as before.
    Require(BaseTab(true, kClassWarrior, 2, true, 0) == 0, "arms warrior without points: arms");
    Require(BaseTab(true, kClassWarrior, 2, true, 1) == 1, "fury warrior without points: fury");
    Require(BaseTab(true, kClassWarrior, 2, true, 2) == 2 && IsTankTab(kClassWarrior, BaseTab(true, kClassWarrior, 2, true, 2)), "protection warrior: tank tab");
    Require(BaseTab(true, kClassWarrior, 2, true, -1) == 2, "warrior without a path: the default tab");
    Require(!IsTankTab(kClassWarrior, BaseTab(true, kClassWarrior, 2, true, 0)) && !IsTankTab(kClassWarrior, BaseTab(true, kClassWarrior, 2, true, 1)),
        "arms / fury reaching 10: no tank tab, so no rebuild to the tank kit");

    // 11) With talent points the spec tab decides; the switch off and every other class: untouched.
    Require(BaseTab(true, kClassWarrior, 2, false, 0) == 2 && BaseTab(true, kClassWarrior, 0, false, 2) == 0, "talent points spent: the spec tab");
    Require(BaseTab(false, kClassWarrior, 2, true, 0) == 2, "switch off / other cohort: the default tab");
    Require(BaseTab(true, kClassPaladin, 2, true, 0) == 2 && BaseTab(true, kClassPaladin, 2, true, 1) == 2, "paladin (holy / protection path): retribution default kept");
    Require(BaseTab(true, 11, 0, true, 1) == 0 && BaseTab(true, 5, 1, true, 2) == 1 && BaseTab(true, 7, 1, true, 0) == 1, "druid / priest / shaman: untouched");

    std::cout << "tank_kit_policy_tests passed\n";
    return 0;
}
