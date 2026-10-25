// twow-repo#602 ROT-01 3c (priest Farathil loop, OB-00 24.10.2026): AiPlayerbot.Rotation.DarkRuneGate - "dark rune" is pushed
// only while the rune or its alternative "mana potion" is usable; a failed check waits RecheckSeconds.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "DarkRuneGatePolicy.h"

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
    using namespace ai::dark_rune_gate;

    // Mode: 0 off, 1 all, 2 cohort B (odd low guid); unknown values act like 0.
    Require(!Applies(0, 8) && !Applies(0, 9), "mode 0 applies to nobody");
    Require(Applies(1, 8) && Applies(1, 9), "mode 1 applies to everybody");
    Require(!Applies(2, 8) && Applies(2, 9), "mode 2 applies to cohort B only");
    Require(!Applies(3, 9) && !Applies(7, 8), "an unknown mode applies to nobody");

    // Wait: a bot that never failed is due; after a failed check at t the next one is due at t + RecheckSeconds.
    std::int64_t const t = 1792900000;
    Require(Due(t, 0), "never failed = due");
    std::int64_t const next = NextCheck(t, 10);
    Require(next == t + 10, "next check after RecheckSeconds");
    Require(!Due(t, next) && !Due(t + 9, next), "no check inside the wait");
    Require(Due(t + 10, next) && Due(t + 60, next), "due once the wait has passed");
    Require(Due(t, NextCheck(t, 0)), "RecheckSeconds 0 = no wait");

    // Required level (0 = none; the Dark Rune 20520 has required_level 0 in tw_world, item_level 55 is no level gate).
    Require(!LevelOk(18, 45), "below a required level");
    Require(LevelOk(45, 45) && LevelOk(60, 45), "at or above the required level");
    Require(LevelOk(1, 0) && LevelOk(18, 0), "no required level");

    // Counters start at zero.
    Require(checks.load() == 0 && passed.load() == 0 && blocked.load() == 0 && waited.load() == 0, "counters start at 0");

    std::cout << "dark_rune_gate_policy tests passed\n";
    return 0;
}
