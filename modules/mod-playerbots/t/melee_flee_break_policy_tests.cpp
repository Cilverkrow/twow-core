// twow-repo#602 ROT-01 (death analysis 21.10.2026, OB-10/OB-00): AiPlayerbot.Rotation.MeleeFleeBreak - "outnumbered" stays
// quiet for a melee bot while its flee loop holds (the same loop test as FleeMultiplier), so it fights instead of fleeing to death.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "MeleeFleeBreakPolicy.h"

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
    using namespace ai::melee_flee_break;
    std::int64_t const window = 7;   // ReturnDelay 7000 ms

    // 1. Applies: melee only; mode 0 off, 1 all, 2 cohort B (odd guid), unknown modes off.
    Require(!Applies(0, 3, false) && !Applies(0, 4, false), "mode 0 off");
    Require(Applies(1, 3, false) && Applies(1, 4, false), "mode 1 every melee bot");
    Require(Applies(2, 3, false) && !Applies(2, 4, false), "mode 2 cohort B only");
    Require(!Applies(1, 3, true) && !Applies(2, 3, true), "ranged bots keep FleeMultiplier's own break");
    Require(!Applies(3, 3, false) && !Applies(7, 4, false), "unknown mode off");

    // 2. Holds: no flee yet, one flee, two flees in the window, the window boundary, an old flee.
    Require(!Holds(1000, 0, 0, window), "never fled");
    Require(!Holds(1000, 0, 5, window), "no last flee attempt, count ignored");
    Require(!Holds(1000, 1000, 1, window), "one flee dispatch: the first attempt is allowed");
    Require(Holds(1000, 1000, 2, window), "second dispatch in a row: the loop holds");
    Require(Holds(1007, 1000, 2, window), "still inside the window (<= ReturnDelay, as FleeMultiplier)");
    Require(!Holds(1008, 1000, 2, window), "window passed: a new attempt is allowed");
    Require(Holds(1003, 1000, 9, window), "a long loop holds as well");

    // 3. The cycle: two flee steps, then the hold until the window has passed (Flee() counts a dispatch within the
    //    window as subsequent, else it starts again at 1).
    std::int64_t last = 0;
    std::uint32_t count = 0;
    auto dispatch = [&](std::int64_t now) { count = (last && now - last <= window) ? count + 1 : 1; last = now; };
    Require(!Holds(100, last, count, window), "fight turns: flee allowed");
    dispatch(100);
    Require(!Holds(100, last, count, window), "after one step: second step allowed");
    dispatch(100);
    Require(Holds(100, last, count, window) && Holds(105, last, count, window), "after two steps: hold");
    Require(!Holds(108, last, count, window), "window over: one new attempt");
    dispatch(108);
    Require(count == 1 && !Holds(108, last, count, window), "the new attempt starts a new count");

    std::cout << "melee_flee_break_policy_tests passed\n";
    return 0;
}
