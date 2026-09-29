#include "ShamanWeaponPolicy.h"

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
    using namespace ai::shaman_weapons;

    Require(!SwordAllowed(false, false, false), "no talent: no one-handed sword");
    Require(!SwordAllowed(true, false, false), "no talent: no two-handed sword");
    Require(SwordAllowed(false, true, true), "with the talent: one-handed sword");
    Require(SwordAllowed(true, true, true), "with the talent: two-handed sword");
    Require(!SwordAllowed(true, true, false), "a two-handed sword needs skill 55");
    Require(!SwordAllowed(false, false, true), "a one-handed sword needs skill 43");

    std::cout << "shaman_weapon_policy_tests passed\n";
    return 0;
}
