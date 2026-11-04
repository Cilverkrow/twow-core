#include "CombatStuckClockPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#575 / #601 (AiPlayerbot.Unstuck.CombatFightClock): the fight clock of "combat stuck" / "combat long stuck".
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

constexpr std::int64_t T0 = 1'800'000'000;   // some wall-clock second
constexpr std::uint64_t MobA = 0xF130000BBA000001ull;
constexpr std::uint64_t MobB = 0xF130000BBA000002ull;
}

int main()
{
    using namespace ai::combat_stuck_clock;

    // 1. The fight counts from its combat start, checked every few seconds with the same target.
    {
        Clock c;
        Require(Seconds(c, T0 + 2, T0, MobA) == 2, "first check counts from the combat start");
        Require(Seconds(c, T0 + 7, T0, MobA) == 7, "same fight, same target");
        std::uint32_t s = 0;
        for (std::int64_t t = T0 + 12; t <= T0 + 910; t += 5)
            s = Seconds(c, t, T0, MobA);
        Require(s > 15 * 60, "a 15-minute fight with one target reaches the long stuck limit");
    }

    // 2. The bug case: an earlier fight 20 min ago, combat ended unseen (the value was set to 0 and back), a new fight starts.
    {
        Clock c;
        Seconds(c, T0 + 1, T0, MobA);
        Seconds(c, T0 + 10, T0, MobA);
        std::int64_t const later = T0 + 20 * 60;
        Require(Seconds(c, later + 1, later, MobB) == 1, "a new combat start value starts a new fight (no clock from the old one)");
    }

    // 3. Unknown start ("combat start time" 0, e.g. after Reset): the fight counts from the first check, fail safe.
    {
        Clock c;
        Require(Seconds(c, T0, 0, MobA) == 0, "unknown start: counts from the first check");
        Require(Seconds(c, T0 + 5, 0, MobA) == 5, "unknown start: then runs on");
    }

    // 4. A stale start value (unchanged across a gap > 30 s) is not this fight: the new fight starts at the check.
    {
        Clock c;
        Seconds(c, T0 + 1, T0, MobA);
        Seconds(c, T0 + 6, T0, MobA);
        Require(Seconds(c, T0 + 6 + NewFightGapSeconds + 1, T0, MobA) == 0, "gap > 30 s with the same stale start value: new fight now");
        Require(Seconds(c, T0 + 6 + NewFightGapSeconds + 4, T0, MobA) == 3, "and it runs from there");
    }

    // 5. A gap of exactly 30 s is still the same fight (the triggers are checked every few seconds).
    {
        Clock c;
        Seconds(c, T0 + 1, T0, MobA);
        Require(Seconds(c, T0 + 1 + NewFightGapSeconds, T0, MobA) == 1 + NewFightGapSeconds, "30 s gap: same fight");
    }

    // 6. A target change restarts the clock; changing back restarts it again (each target counts on its own).
    {
        Clock c;
        for (std::int64_t t = T0 + 1; t < T0 + 200; t += 5)
            Seconds(c, t, T0, MobA);
        Require(Seconds(c, T0 + 200, T0, MobA) == 200, "200 s on mob A");
        Require(Seconds(c, T0 + 205, T0, MobB) == 0, "switch to mob B restarts");
        Require(Seconds(c, T0 + 210, T0, MobB) == 5, "mob B runs");
        Require(Seconds(c, T0 + 215, T0, MobA) == 0, "back to mob A restarts again");
        Require(Seconds(c, T0 + 218, T0, 0) == 0, "losing the target is a change too");
    }

    // 7. A start value in the future (clock skew) is not taken: the fight starts at the check.
    {
        Clock c;
        Require(Seconds(c, T0, T0 + 50, MobA) == 0, "future start value ignored");
    }

    // 8. A start value that appears while the clock already runs cannot move the anchor before the previous check.
    {
        Clock c;
        Seconds(c, T0 + 100, 0, MobA);          // unknown start, counting from T0+100
        Seconds(c, T0 + 105, 0, MobA);
        Require(Seconds(c, T0 + 110, T0, MobA) == 5, "late start value: anchor not before the previous check (T0+105)");
    }

    // 9. Two clocks (the 5-min and the 15-min trigger) are independent and agree on the same inputs.
    {
        Clock shortClock, longClock;
        for (std::int64_t t = T0; t <= T0 + 400; t += 5)
        {
            std::uint32_t const a = Seconds(shortClock, t, T0, MobA);
            std::uint32_t const b = Seconds(longClock, t, T0, MobA);
            Require(a == b, "both triggers see the same fight time");
        }
        Require(Seconds(shortClock, T0 + 405, T0, MobA) > 5 * 60, "the 5-minute trigger can still fire in a long fight");
    }

    std::cout << "combat_stuck_clock_policy_tests: ok\n";
    return 0;
}
