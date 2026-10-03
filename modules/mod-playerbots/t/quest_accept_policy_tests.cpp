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

    // Hotfix 8.11: accept rule = drop rule (red or XP-grey), class quests allowed.
    Require(SkipForRosterBot(true, 15, 7, 9, false), "The Grizzled Den (7) at level 15: grey, skipped");
    Require(SkipForRosterBot(true, 15, 9, 9, false), "at the grey level: skipped");
    Require(!SkipForRosterBot(true, 15, 10, 9, false), "above the grey level: taken");
    Require(SkipForRosterBot(true, 14, 19, 8, false), "red: skipped");
    Require(!SkipForRosterBot(true, 15, 7, 9, true), "class quest: taken");
    Require(!SkipForRosterBot(false, 15, 7, 9, false), "led by a real player: taken");

    std::cout << "quest_accept_policy_tests passed\n";
    return 0;
}
