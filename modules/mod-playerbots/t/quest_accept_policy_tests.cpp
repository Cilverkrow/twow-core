#include "QuestAcceptPolicy.h"

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
    using namespace ai::quest_accept;

    // CleanQuestLogAction keeps a quest when botLevel + 5 > questLevel.
    Require(!IsRed(14, 18), "four levels above: kept");
    Require(IsRed(14, 19), "five levels above: red, dropped by the cleanup");
    Require(IsRed(14, 21), "Consumed by Hatred for a level 14 bot");
    Require(!IsRed(14, 14) && !IsRed(14, 3), "own level and grey quests are not red");
    Require(!IsRed(60, 60), "level 60");

    std::cout << "quest_accept_policy_tests passed\n";
    return 0;
}
