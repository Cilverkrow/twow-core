// twow-repo#654 (RogueTankStancePolicy.h, owner 29.10.2026, Rotation.RogueTankStance default 0, A/B with 2): a bot on the rogue tank
// path learns the Shadow Dance stance from 20 when the core switch covers bots, keeps it up while it tanks, drops it otherwise, and does
// not try Stealth / Vanish in it. Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "RogueTankStancePolicy.h"

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
    using namespace ai::rogue_tank_stance;

    // 1) The ids match the core (FunserverShadowDanceStance.h) and Shadow Dance rank 1.
    Require(StanceSpell == 61230 && MinLevel == 20, "stance 61230 from level 20");

    // 2) The bot switch: 0 off, 1 every bot, 2 cohort B (odd guid) only.
    Require(!Applies(0, 3) && Applies(1, 4) && Applies(2, 3) && !Applies(2, 4), "bot switch modes");

    // 3) The core switch covers bots with 1 (every rogue) and 2 (bots only), not with 0.
    Require(!CoreCoversBots(0) && CoreCoversBots(1) && CoreCoversBots(2) && !CoreCoversBots(3), "core switch modes");

    // 4) Teaching: switch, core switch, the rogue tank path, level 20 - all four.
    Require(Teach(true, true, true, 20) && Teach(true, true, true, 60), "tank path from 20: taught");
    Require(!Teach(true, true, true, 19), "below 20: not yet");
    Require(!Teach(true, true, false, 60), "another path (DPS rogue): never");
    Require(!Teach(true, false, true, 60), "core switch 0: never");
    Require(!Teach(false, true, true, 60), "bot switch off: never");

    // 5) Up only while tanking; otherwise cancelled.
    Require(WantsUp(true, true, true), "tanking: the stance stays up");
    Require(!WantsUp(true, true, false), "not tanking: down");
    Require(!WantsUp(true, false, true) && !WantsUp(false, true, true), "a switch no longer covers the bot: down");
    Require(Cancel(true, false) && !Cancel(true, true) && !Cancel(false, false), "cancel only an unwanted, active stance");

    // 6) Owner "Nur Stealth und Vanish": not useful in the stance, useful outside.
    Require(!StealthUseful(true) && StealthUseful(false), "no Stealth / Vanish attempt in the stance");

    std::cout << "rogue_tank_stance_policy: all checks passed\n";
    return 0;
}
