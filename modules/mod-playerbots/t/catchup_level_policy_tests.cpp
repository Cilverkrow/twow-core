#include "CatchupLevelPolicy.h"

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

// MaNGOS::XP::GetGrayLevel (src/game/Formulas.h) for the levels used here.
unsigned Gray(unsigned level)
{
    if (level <= 5)
        return 0;
    if (level <= 39)
        return level - 5 - level / 10;
    return level - 1 - level / 5;
}
}

int main()
{
    using namespace ai::catchup_level;

    // GM check 3 (26.11.2026): 40471 "The Harvest Golem Mystery", quest level 14, master Uthery L20.
    Require(Gray(20) == 13 && Gray(21) == 14 && Gray(22) == 15, "grey levels of the edge");
    Require(Admits(20, 20, 14, Gray(20)), "same level (Merelyn): admitted");
    Require(Admits(20, 19, 14, Gray(19)), "one below (Evandril): admitted");
    Require(!Admits(20, 21, 14, Gray(21)), "Tamsin L21: quest 14 is grey for her - refused");
    Require(Admits(20, 21, 15, Gray(21)), "L21 bot, quest 15: green - admitted (new)");
    Require(!GreyFor(14, Gray(20)) && GreyFor(14, Gray(21)), "grey edge: 14 green at L20, grey at L21");

    // The window below the master stays.
    Require(Admits(20, 12, 14, Gray(12)), "8 below: admitted");
    Require(!Admits(20, 11, 14, Gray(11)), "9 below: refused");

    // Above the master only the grey rule counts.
    Require(Admits(20, 25, 22, Gray(25)), "5 above, quest still green: admitted");
    Require(Gray(25) == 18, "grey level at L25");
    Require(Admits(20, 25, 19, Gray(25)), "5 above, quest 19 one over grey: admitted");
    Require(!Admits(20, 25, 18, Gray(25)), "5 above, quest 18 grey: refused");
    Require(Admits(20, 23, -1, Gray(23)), "scaling quest (level -1) is never grey");
    Require(Admits(20, 23, 0, Gray(23)), "quest level 0 is never grey");
    return 0;
}
