// twow-repo#587 Z4 (AiPlayerbot.ZoneExit.TurnInStallCounts): stage-1 recoveries of an unreachable turn-in, counted as
// turn-in failures by the #485 park book, park the turn-in after TurnInParkFailures within the window.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestTurnInParkPolicy.h"
#include "UnreachableTargetPolicy.h"

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
    using namespace ai::turnin_park;
    // Live values: TurnInParkFailures 3, TurnInParkWindowSeconds 3600, TurnInParkSeconds 3600.
    std::uint32_t const minute = 60000, window = 60 * minute, park = 60 * minute;

    // Aurelith (130), quest 1097: a stage-1 recovery about every 5 minutes.
    Book book;
    std::uint32_t const t = 5000000;
    Require(!RecordFailure(book, 1097, t, 3, window, park), "first recovery: not parked");
    Require(!RecordFailure(book, 1097, t + 5 * minute, 3, window, park), "second recovery: not parked");
    Require(RecordFailure(book, 1097, t + 10 * minute, 3, window, park), "third recovery within the hour: parked");
    Require(IsParked(book, 1097, t + 11 * minute) && IsParked(book, 1097, t + 69 * minute), "parked for an hour");
    Require(!IsParked(book, 1097, t + 71 * minute), "back after the park");

    // Negative: other quests are untouched; recoveries far apart never park.
    Require(!IsParked(book, 1098, t + 11 * minute), "another quest is not parked");
    Book slow;
    bool parked = false;
    for (std::uint32_t i = 0; i < 6; ++i)
        parked = RecordFailure(slow, 1097, t + i * 70 * minute, 3, window, park) || parked;
    Require(!parked, "one recovery every 70 minutes: never parked");

    // [UnreachableTop]: targets by recoveries, the top n, the book empty afterwards; one taker = one key.
    namespace ut = ai::unreachable_target;
    ut::Book top;
    ut::Key const elmore = ut::KeyOf(1416, 0, -8387.1f, 685.2f);   // Grimand Elmore, the taker of 1097
    for (std::uint32_t i = 0; i < 8; ++i)
        top.Add(ut::KeyOf(1416, 0, -8387.1f, 685.2f), 1097, 130 + (i % 3));   // the same taker, 3 bots
    top.Add(ut::KeyOf(240, 0, -9465.0f, 73.0f), 40, 7);
    std::uint32_t total = 0;
    auto const best = top.TakeTop(1, total);
    Require(total == 9 && best.size() == 1 && !(best[0].first < elmore) && !(elmore < best[0].first), "the Stormwind taker on top");
    Require(best[0].second.count == 8 && best[0].second.bots.size() == 3 && best[0].second.quest == 1097, "count, bots, quest");
    Require(ut::Format(best) == "1416:0:-8390:690:1097:8:3", "line format entry:map:x:y:quest:count:bots");
    std::uint32_t again = 0;
    Require(top.TakeTop(5, again).empty() && again == 0, "the window starts empty again");

    std::cout << "turnin_stall_policy_tests passed\n";
    return 0;
}
