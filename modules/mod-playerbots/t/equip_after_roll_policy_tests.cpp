#include "EquipAfterRollPolicy.h"

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
    using ai::equip_after_roll::Equips;

    // twow-repo#341 (owner repro 25.10.2026): a won roll on gear runs "equip upgrades".
    Require(Equips(true, true, true), "own win of a weapon, armour piece or bag: equip upgrades");
    Require(!Equips(false, true, true), "switch off: as before (nothing)");
    Require(!Equips(true, false, true), "someone else won (the packet goes to every voter): nothing - no item is taken from the player");
    Require(!Equips(true, true, false), "own win of no gear (recipe, trade goods): nothing, as for a looted one");

    std::cout << "equip_after_roll_policy_tests passed\n";
    return 0;
}
