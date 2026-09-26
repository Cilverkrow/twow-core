#include "ClassGrantPolicy.h"

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
    using namespace ai::class_grant;
    auto const& totems = ShamanTotems();

    // Turtle quest levels (tw_world, OB-00 26.09.): Earth 4, Fire 10, Water 20, Air 30.
    Require(totems[0].level == 4 && totems[0].item == 5175 && totems[0].teachSpell == 8073, "earth totem at 4");
    Require(totems[1].level == 10 && totems[1].item == 5176 && totems[1].teachSpell == 2075, "fire totem at 10");
    Require(totems[2].level == 20 && totems[2].item == 5177 && totems[2].teachSpell == 5396, "water totem at 20");
    Require(totems[3].level == 30 && totems[3].item == 5178 && totems[3].teachSpell == 0, "air totem at 30, no spell");

    Require(!IsDue(totems[0], 3) && IsDue(totems[0], 4), "earth due from level 4");
    Require(!IsDue(totems[2], 19) && IsDue(totems[2], 20), "water due from level 20");
    Require(IsDue(totems[3], 60), "all due at 60");
    return 0;
}
