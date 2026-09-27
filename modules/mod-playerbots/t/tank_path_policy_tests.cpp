#include "TankPathPolicy.h"

#include <cstdlib>
#include <cstring>
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
    using namespace ai::tank_path;

    // #357: shaman 7.3 "shaman tank" -> "tank shaman".
    Require(IsTankPath(7, "shaman tank"), "shaman tank path is a tank path");
    Require(std::strcmp(StrategyFor(7, "shaman tank"), "tank shaman") == 0, "shaman tank strategy");
    Require(!IsTankPath(7, "enhancement"), "enhancement stays dps");

    // #367: rogue 4.3 "rogue tank" -> "tank rogue".
    Require(IsTankPath(4, "rogue tank"), "rogue tank path is a tank path");
    Require(std::strcmp(StrategyFor(4, "rogue tank"), "tank rogue") == 0, "rogue tank strategy");
    Require(!IsTankPath(4, "combat"), "combat stays dps");

    // A path name only counts for its own class; no path = no tank.
    Require(!IsTankPath(4, "shaman tank"), "shaman path on a rogue is ignored");
    Require(!IsTankPath(7, "rogue tank"), "rogue path on a shaman is ignored");
    Require(!IsTankPath(11, "bear"), "the bear keeps its own AiFactory handling");
    Require(!IsTankPath(7, ""), "no premade path, no tank");
    Require(StrategyFor(1, "protection") == nullptr, "other classes untouched");

    // Owner 2026-09-27: Stormstrike only with aggro and at least 4 shield charges.
    Require(AllowTankStormstrike(true, 4), "aggro and 4 charges allow Stormstrike");
    Require(AllowTankStormstrike(true, 9), "more charges allow it too");
    Require(!AllowTankStormstrike(true, 3), "3 charges are too few");
    Require(!AllowTankStormstrike(false, 9), "no Stormstrike without aggro");

    std::cout << "tank_path_policy_tests passed\n";
    return 0;
}
