// twow-repo#587 Z4 (AiPlayerbot.ZoneExit.TurnInStallCounts): stage-1 recoveries of an unreachable turn-in, counted as
// turn-in failures by the #485 park book, park the turn-in after TurnInParkFailures within the window.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestTurnInParkPolicy.h"

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

    std::cout << "turnin_stall_policy_tests passed\n";
    return 0;
}
