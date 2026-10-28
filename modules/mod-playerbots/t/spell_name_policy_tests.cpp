// twow-repo#602 ROT-01 spell-name audit (SpellNamePolicy.h, OB-00 28.10.2026, bug fix): the spell names that resolve in the image's spell data
// (Barkskin, Hand of Freedom / Protection / Sacrifice) and the warlock stone ranks, highest first.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "SpellNamePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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
    using namespace ai::spell_names;

    // 1) The corrected names (lower case, as the "spell id" value matches them).
    Require(std::string(kBarkskin) == "barkskin", "Barkskin (22812, druid L44)");
    Require(std::string(kHandOfFreedom) == "hand of freedom", "Hand of Freedom (1044, paladin L18)");
    Require(std::string(kHandOfProtection) == "hand of protection", "Hand of Protection (1022 / 5599 / 10278, paladin L10 / 24 / 38)");
    Require(std::string(kHandOfSacrifice) == "hand of sacrifice", "Hand of Sacrifice (6940 / 20729, paladin L46 / 54)");

    // 2) Stone ranks, highest first; the bare name is one rank in the middle (healthstone 6201 L10 .. 11730 L58).
    auto const hs = StoneRankNames("create healthstone");
    Require(hs[0] == "create healthstone (major)" && hs[1] == "create healthstone (greater)", "major, greater first");
    Require(hs[2] == "create healthstone", "the bare name (5699, L34) in the middle");
    Require(hs[3] == "create healthstone (lesser)" && hs[4] == "create healthstone (minor)", "lesser, minor last (minor = L10)");
    Require(StoneRankNames("create soulstone")[4] == "create soulstone (minor)", "soulstone minor (693, L18)");

    std::cout << "spell_name_policy_tests passed\n";
    return 0;
}
