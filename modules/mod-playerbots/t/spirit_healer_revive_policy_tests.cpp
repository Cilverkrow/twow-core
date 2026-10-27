// Owner order 27.10.2026 (SpiritHealerRevivePolicy.h, AiPlayerbot.Revive.SpiritHealerFull, code default 1): a bot revived by a
// spirit healer comes back at full health and mana without Resurrection Sickness; 0 keeps half health and mana and the sickness.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "SpiritHealerRevivePolicy.h"

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
    using namespace ai::spirit_healer;

    // 1) The mode: 0 off, 1 every bot, 2 only cohort B (odd low guid); any other mode applies to nobody here (the config
    //    read maps it to the default 1 before).
    Require(!FullApplies(0, 2) && !FullApplies(0, 3), "mode 0: no bot");
    Require(FullApplies(1, 2) && FullApplies(1, 3), "mode 1: every bot");
    Require(!FullApplies(2, 2) && FullApplies(2, 3), "mode 2: only the odd guid");
    Require(!FullApplies(7, 3), "an unknown mode applies to nobody");

    // 2) Full revive: 100 % and no sickness, with or without the repair cheat.
    Require(RestorePercent(true) == 1.0f, "full: 100 %");
    Require(!ApplySickness(true, false) && !ApplySickness(true, true), "full: never the sickness");

    // 3) Off: the old revive - 50 %, the sickness unless the bot has the repair cheat.
    Require(RestorePercent(false) == 0.5f, "off: 50 %");
    Require(ApplySickness(false, false), "off: the sickness without the repair cheat");
    Require(!ApplySickness(false, true), "off: no sickness with the repair cheat");

    std::cout << "spirit_healer_revive_policy tests passed\n";
    return 0;
}
