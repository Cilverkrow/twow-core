#include "ReviveChoicePolicy.h"

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
    using namespace ai::revive_choice;

    Require(ShortcutToSpiritHealerAllowed(false, true, 20, 1), "switch off: shortcuts as before");
    Require(ShortcutToSpiritHealerAllowed(true, false, 20, 1), "not a roster bot on its own: as before");
    Require(ShortcutToSpiritHealerAllowed(true, true, 10, 1), "level 10: no resurrection sickness, as before");
    Require(!ShortcutToSpiritHealerAllowed(true, true, 11, 1), "level 11, first death: corpse run");
    Require(!ShortcutToSpiritHealerAllowed(true, true, 40, 0), "death count not yet raised: corpse run");
    Require(ShortcutToSpiritHealerAllowed(true, true, 40, 2), "second death in a row: shortcuts allowed");

    std::cout << "revive_choice_policy_tests passed\n";
    return 0;
}
