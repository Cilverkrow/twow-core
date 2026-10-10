// twow-repo#541 A39: death loop escape - safe starts of the rebound races and the repeat-after-escape window.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "DeathLoopPolicy.h"

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
    using namespace ai::death_loop;

    StartPoint start;
    Require(RaceStartOverride(9, start) && start.map == 1 && start.zone == 14, "goblin: Durotar (map 1, zone 14)");
    Require(RaceStartOverride(10, start) && start.map == 0 && start.zone == 12, "high elf: Elwynn Forest (map 0, zone 12)");
    for (std::uint32_t race : { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u })
        Require(!RaceStartOverride(race, start), "other races use their race start");

    Require(!RepeatAfterEscape(1000, 0, 900), "no escape yet: no repeat");
    Require(RepeatAfterEscape(1000, 1000, 900), "death right after the escape");
    Require(RepeatAfterEscape(1900, 1000, 900), "death at the end of the window");
    Require(!RepeatAfterEscape(1901, 1000, 900), "death after the window: a new loop, not a repeat");
    Require(!RepeatAfterEscape(900, 1000, 900), "clock before the escape: no repeat");

    for (int count = 0; count < EscapeCounts; ++count)
        Require(EscapeCounter(EscapeCount(count)).load() == 0, "counters start at 0");
    EscapeCounter(EscapeDetected).fetch_add(2);
    Require(EscapeCounter(EscapeDetected).exchange(0) == 2 && EscapeCounter(EscapeDetected).load() == 0, "minute line takes and resets");

    std::cout << "deathloop_escape_policy_tests passed\n";
    return 0;
}
