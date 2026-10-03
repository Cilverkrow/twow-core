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

    // Hotfix 8.15: no new quest at the cleanup fill level (4 or fewer free slots).
    Require(!SkipForRosterBot(true, 15, 12, 9, false, 5), "5 free slots: taken");
    Require(SkipForRosterBot(true, 15, 12, 9, false, 4), "4 free slots: skipped (cleanup would drop)");
    Require(SkipForRosterBot(true, 15, 12, 9, false, 0), "full log: skipped");
    Require(!SkipForRosterBot(true, 15, 12, 9, true, 2), "class quest even with a fuller log: taken");
    Require(!SkipForRosterBot(false, 15, 12, 9, false, 1), "led by a real player: taken");
    Require(CleanFreeSlots == 4, "limit matches CleanQuestLogAction (MAX_QUEST_LOG_SIZE - totalQuests > 4)");

    // Hotfix 8.17: rotation only while stuck with a full log, once per 20 minutes.
    uint32_t const T = 100000;
    Require(IdleRotateDue(true, false, IdleRotateSeconds, 16, 20, 0, T), "16 quests, 20 min idle: rotate");
    Require(!IdleRotateDue(true, false, IdleRotateSeconds - 1, 16, 20, 0, T), "under 20 min idle: no");
    Require(!IdleRotateDue(true, false, IdleRotateSeconds, 15, 20, 0, T), "15 quests (accepting again): no");
    Require(!IdleRotateDue(true, true, IdleRotateSeconds, 18, 20, 0, T), "in combat: no");
    Require(!IdleRotateDue(false, false, IdleRotateSeconds, 18, 20, 0, T), "led by a real player: no");
    Require(!IdleRotateDue(true, false, 99999, 18, 20, T - IdleRotateSeconds + 1, T), "once per 20 minutes");
    Require(IdleRotateDue(true, false, 99999, 18, 20, T - IdleRotateSeconds, T), "after 20 minutes again");

    Require(PickIdleRotate({}) == -1, "no candidate: none");
    {
        RotateCandidate a; a.slot = 2;
        RotateCandidate b; b.slot = 7; b.otherZone = true;
        RotateCandidate c; c.slot = 1;
        Require(PickIdleRotate({ a, b, c }) == 1, "a quest of another zone first");
        Require(PickIdleRotate({ a, c }) == 1, "else the oldest (lowest slot)");
        RotateCandidate d; d.slot = 4; d.otherZone = true;
        Require(PickIdleRotate({ b, d }) == 1, "two of another zone: the lower slot");
    }

    std::cout << "quest_accept_policy_tests passed\n";
    return 0;
}
