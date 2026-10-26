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

    // twow-repo#601/#602 G2: short life after a revive (Amelleni, track A 26.10.: revive at the spirit healer, dead 29 s,
    // 15 s, 14 s later within ~60 yd; the window rule never saw 4 deaths in 900 s).
    {
        ShortLife life;
        Require(!IsShortLifeLoop(life), "no streak yet");
        Require(!RecordShortLifeDeath(life, 1000, 1, 0.0f, 0.0f, 150.0f) && life.streak == 0, "a death without a revive is no short life");

        RecordRevive(life, 2000, 1, -125.0f, -7550.0f);
        Require(!RecordShortLifeDeath(life, 2029, 1, -72.0f, -7551.0f, 150.0f) && life.streak == 1, "1st short life");
        Require(life.reviveAt == 0, "the revive is used up by the death");
        Require(!RecordShortLifeDeath(life, 2040, 1, -72.0f, -7551.0f, 150.0f) && life.streak == 0, "a death with no revive in between ends the streak");

        RecordRevive(life, 3000, 1, -125.0f, -7550.0f);
        Require(!RecordShortLifeDeath(life, 3029, 1, -72.0f, -7551.0f, 150.0f) && life.streak == 1, "streak restarts at 1");
        RecordRevive(life, 3600, 1, -125.0f, -7550.0f);
        Require(!RecordShortLifeDeath(life, 3615, 1, -100.0f, -7600.0f, 150.0f) && life.streak == 2, "2nd short life (~10 min of ghost time between)");
        RecordRevive(life, 4200, 1, -130.0f, -7545.0f);
        Require(RecordShortLifeDeath(life, 4214, 1, -110.0f, -7540.0f, 150.0f) && life.streak == 3, "3rd short life: loop");
        Require(IsShortLifeLoop(life), "loop until cleared");

        life = ShortLife();
        RecordRevive(life, 5000, 1, 0.0f, 0.0f);
        Require(!RecordShortLifeDeath(life, 5061, 1, 0.0f, 0.0f, 150.0f) && life.streak == 0, "61 s alive: no short life");
        RecordRevive(life, 6000, 1, 0.0f, 0.0f);
        Require(!RecordShortLifeDeath(life, 6060, 1, 0.0f, 0.0f, 150.0f) && life.streak == 1, "60 s alive: short life");
        RecordRevive(life, 7000, 1, 0.0f, 0.0f);
        Require(!RecordShortLifeDeath(life, 7010, 1, 151.0f, 0.0f, 150.0f) && life.streak == 0, "death beyond the radius of the revive: none");
        RecordRevive(life, 8000, 1, 0.0f, 0.0f);
        Require(!RecordShortLifeDeath(life, 8010, 0, 0.0f, 0.0f, 150.0f) && life.streak == 0, "death on another map: none");
        RecordRevive(life, 9000, 1, 0.0f, 0.0f);
        Require(!RecordShortLifeDeath(life, 8990, 1, 0.0f, 0.0f, 150.0f) && life.streak == 0, "clock before the revive: none");

        life = ShortLife();
        RecordRevive(life, 10000, 1, 0.0f, 0.0f);
        RecordShortLifeDeath(life, 10010, 1, 0.0f, 0.0f, 150.0f);
        RecordRevive(life, 11000, 1, 0.0f, 0.0f);
        RecordShortLifeDeath(life, 11010, 1, 0.0f, 0.0f, 150.0f);
        RecordRevive(life, 12000, 1, 400.0f, 0.0f);
        Require(!RecordShortLifeDeath(life, 12010, 1, 400.0f, 0.0f, 150.0f) && life.streak == 1, "a revive far from the streak's first one starts a new streak");
        Require(life.anchorX == 400.0f, "the new streak's anchor");

        RecordRevive(life, 0, 1, 0.0f, 0.0f);
        Require(life.reviveAt != 0, "a revive at clock 0 still counts");
    }

    // G2: the evacuation point at the loop (goblin homebind = island start, 388 yd from the graveyard, zone 5536).
    Require(EscapeAtLoop(1, -125.0f, -7550.0f, 5536, 1, -233.0f, -7177.0f, 5536), "homebind in the loop's zone");
    Require(EscapeAtLoop(1, -125.0f, -7550.0f, 0, 1, -233.0f, -7177.0f, 0), "homebind 388 yd away, zone unknown");
    Require(EscapeAtLoop(1, 0.0f, 0.0f, 14, 1, 599.0f, 0.0f, 17), "599 yd: at the loop");
    Require(!EscapeAtLoop(1, 0.0f, 0.0f, 14, 1, 600.0f, 0.0f, 17), "600 yd in another zone: away");
    Require(!EscapeAtLoop(1, 0.0f, 0.0f, 14, 0, 0.0f, 0.0f, 14), "another map: away");
    Require(!EscapeAtLoop(1, -125.0f, -7550.0f, 5536, 1, -618.518f, -4251.67f, 14), "Durotar safe start: away from the island loop");
    Require(!EscapeAtLoop(1, 0.0f, 0.0f, 0, 1, 700.0f, 0.0f, 0), "unknown zones do not match each other");

    std::cout << "deathloop_escape_policy_tests passed\n";
    return 0;
}
