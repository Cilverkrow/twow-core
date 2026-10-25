// twow-repo#602 ROT-01 hunter deaths (focus check 25.10.2026): AiPlayerbot.Rotation.HunterMeleeStart - the hunter's switch to melee
// really starts the swing, and an attack in "close" mode starts melee.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "HunterMeleeStartPolicy.h"

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
    using namespace ai::hunter_melee_start;

    Require(!Applies(0, 8) && !Applies(0, 9), "mode 0 applies to nobody");
    Require(Applies(1, 8) && Applies(1, 9), "mode 1 applies to everybody");
    Require(!Applies(2, 8) && Applies(2, 9), "mode 2 applies to cohort B only");
    Require(!Applies(3, 9), "an unknown mode applies to nobody");

    // The old rule is unchanged: a melee bot always, a ranged bot only under 5 yd.
    Require(AttackWithMelee(false, 20.0f, false, false), "melee bot");
    Require(AttackWithMelee(true, 4.9f, false, false), "ranged bot under 5 yd");
    Require(!AttackWithMelee(true, 5.0f, false, false), "ranged bot at 5 yd");
    Require(!AttackWithMelee(true, 25.0f, false, true), "close mode without the switch = the old rule");
    // With the switch a hunter in close mode starts melee at any distance; ranged mode stays ranged.
    Require(AttackWithMelee(true, 8.0f, true, true), "close mode with the switch");
    Require(!AttackWithMelee(true, 25.0f, true, false), "ranged mode with the switch stays ranged");

    Require(started.load() == 0 && stopped.load() == 0 && closeAttacks.load() == 0, "counters start at 0");

    std::cout << "hunter_melee_start_policy tests passed\n";
    return 0;
}
