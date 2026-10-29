// twow-repo#654 step 1 (TankRoguePoisonPolicy.h, OB-00 / owner 29.10.2026, Rotation.TankRoguePoison default 1, 0 = rollback): the rogue
// tank's multiplier keeps every main-hand poison but the threat poison off, told by the action (hand + threat poison).
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "TankRoguePoisonPolicy.h"

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
    using namespace ai::tank_rogue_poison;

    // 1) The switch: 0 off (rollback), 1 every bot, 2 cohort B (odd low guid) only.
    Require(!Applies(0, 3) && !Applies(0, 4), "mode 0: off for every bot");
    Require(Applies(1, 3) && Applies(1, 4), "mode 1: every bot");
    Require(Applies(2, 3) && !Applies(2, 4), "mode 2: cohort B (odd guid) only");

    // 2) Another main-hand poison (Instant, Deadly, Wound, ...) is blocked: it would replace the threat poison.
    Require(BlocksMainHand(true, true, false), "a foreign main-hand poison is blocked");

    // 3) The threat poison itself stays, and the off hand is never touched.
    Require(!BlocksMainHand(true, true, true), "the threat poison (Agitating) on the main hand stays");
    Require(!BlocksMainHand(true, false, false), "an off-hand poison stays");
    Require(!BlocksMainHand(true, false, true), "an off-hand threat poison stays");

    // 4) Any other action is not a poison: never blocked by this rule.
    Require(!BlocksMainHand(false, true, false) && !BlocksMainHand(false, false, false), "a non-poison action is never blocked");

    std::cout << "tank_rogue_poison_policy: all checks passed\n";
    return 0;
}
