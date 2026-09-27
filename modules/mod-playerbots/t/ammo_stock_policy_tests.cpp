#include "AmmoStockPolicy.h"

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
    using ai::ammo_stock::NeededStacks;

    // Class defaults without a cap (0).
    Require(NeededStacks(true, false, 0, 0) == 8.0f, "hunter ammo default: 8 stacks");
    Require(NeededStacks(false, true, 0, 0) == 2.0f, "warrior/rogue thrown default: 2 stacks");

    // Shipped caps: 2 stacks of ammo, 1 stack of thrown weapons (OB-10 train 6).
    Require(NeededStacks(true, false, 2, 1) == 2.0f, "hunter ammo capped at 2 stacks");
    Require(NeededStacks(false, true, 2, 1) == 1.0f, "thrown weapons capped at 1 stack");
    Require(NeededStacks(false, false, 2, 1) == 2.0f, "non-hunter ammo: 2 stacks");

    // A cap never raises the need.
    Require(NeededStacks(false, true, 2, 5) == 2.0f, "cap above the default keeps the default");
    Require(NeededStacks(true, false, 1, 1) == 1.0f, "cap of one stack");
    return 0;
}
