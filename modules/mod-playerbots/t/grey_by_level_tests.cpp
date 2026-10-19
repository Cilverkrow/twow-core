// twow-repo#601 / #614: grey by the mob's level (QuestSearchPolicy.h GreyByLevel), as the core colours it.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestSearchPolicy.h"

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

// The core's MaNGOS::XP::GetGrayLevel (Formulas.h), copied for the test (no game headers here).
std::uint32_t GrayLevel(std::uint32_t level)
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
    using ai::quest_search::GreyByLevel;

    // OB-00 19.10.: the L1-b case - an untouched level-2 wolf is not grey for a level-1 bot (XP::Gain said 0).
    Require(!GreyByLevel(2, GrayLevel(1)), "L1 bot, L2 wolf: not grey");
    Require(!GreyByLevel(1, GrayLevel(1)), "L1 bot, L1 mob: not grey");
    Require(!GreyByLevel(1, GrayLevel(5)), "below level 6 nothing is grey");
    // L10: grey level 4
    Require(GreyByLevel(4, GrayLevel(10)) && !GreyByLevel(5, GrayLevel(10)), "L10 bot: L4 grey, L5 not");
    // L14 (Deygo, #421): grey level 8 - level-1 wolves are grey
    Require(GreyByLevel(1, GrayLevel(14)) && GreyByLevel(8, GrayLevel(14)) && !GreyByLevel(9, GrayLevel(14)), "L14 bot");
    // L60: grey level 47
    Require(GreyByLevel(47, GrayLevel(60)) && !GreyByLevel(48, GrayLevel(60)), "L60 bot");

    // CLI-Deep review: no kill reward or a zero XP template stays "no XP" even when not grey by level.
    using ai::quest_search::NoXpTarget;
    Require(!NoXpTarget(false, false, false), "a green mob with a kill reward: XP");
    Require(NoXpTarget(true, false, false), "grey: no XP");
    Require(NoXpTarget(false, true, false), "no kill reward: no XP");
    Require(NoXpTarget(false, false, true), "zero XP template: no XP");

    std::cout << "grey_by_level_tests passed\n";
    return 0;
}
