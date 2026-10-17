// twow-repo#308 EQUIP-FLIP-01: the hands come off only for a hand-item candidate in the bags (with the switch).
#include <cstdlib>
#include <iostream>

#include "KeepWeaponsPolicy.h"

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
    using namespace ai::keep_weapons;

    // Perpeper (warrior L4): Worn Shortsword + Worn Wooden Shield on, a looted belt, nothing for the hands in the bags.
    Require(!UnequipHands(true, false), "switch on, no hand candidate: weapons stay on");
    Require(UnequipHands(true, true), "switch on, a better weapon in the bags: hands come off for the combo check");
    Require(UnequipHands(false, false) && UnequipHands(false, true), "negative: switch off - always, as before");

    Require(IsHandItem(13) && IsHandItem(14) && IsHandItem(17) && IsHandItem(21) && IsHandItem(22) && IsHandItem(23),
        "one hand, shield, two hand, main hand, off hand, held");
    Require(!IsHandItem(6) && !IsHandItem(15) && !IsHandItem(26) && !IsHandItem(0), "negative: belt, ranged, wand, none");

    std::cout << "keep_weapons_policy_tests passed\n";
    return 0;
}
