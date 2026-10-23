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
    Require(!Applies(0, 3, false, false) && !Applies(0, 4, false, false) && !Applies(0, 3, true, true), "mode 0 off, also with Ranged");
    Require(Applies(1, 3, false, false) && Applies(1, 4, false, false), "mode 1 every melee bot");
    Require(Applies(2, 3, false, false) && !Applies(2, 4, false, false), "mode 2 cohort B only");
    Require(!Applies(1, 3, true, false) && !Applies(2, 3, true, false), "ranged bots keep FleeMultiplier's own break");
    Require(Applies(1, 3, true, true) && Applies(1, 4, true, true), "MeleeFleeBreak.Ranged: ranged bots too, mode 1");
    Require(Applies(2, 3, true, true) && !Applies(2, 4, true, true), "MeleeFleeBreak.Ranged follows the cohort of mode 2");
    Require(!Applies(3, 3, false, true) && !Applies(7, 4, true, true), "unknown mode off");

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
    auto dispatch = [&](std::int64_t now) { count = NextCount(now, last, count, window); last = now; };
    Require(!Holds(100, last, count, window), "fight turns: flee allowed");
    dispatch(100);
    Require(!Holds(100, last, count, window), "after one step: second step allowed");
    dispatch(100);
    Require(Holds(100, last, count, window) && Holds(105, last, count, window), "after two steps: hold");
    Require(!Holds(108, last, count, window), "window over: one new attempt");
    dispatch(108);
    Require(count == 1 && !Holds(108, last, count, window), "the new attempt starts a new count");

    // 4. NextCount (MFB-W 23.10.): the same count as Flee() keeps for FleeMultiplier; the random-bot chase branch, which
    //    re-dispatches on every AI tick, reaches the hold after its second dispatch.
    Require(NextCount(100, 0, 0, window) == 1 && NextCount(100, 0, 7, window) == 1, "first dispatch: 1");
    Require(NextCount(107, 100, 1, window) == 2 && NextCount(108, 100, 5, window) == 1, "window edge: subsequent / fresh");
    last = 0;
    count = 0;
    dispatch(200);
    dispatch(200);   // chase branch: the next tick in the same second
    Require(Holds(200, last, count, window) && !Holds(208, last, count, window), "chase loop holds after two ticks, until the window passes");

    std::cout << "melee_flee_break_policy_tests passed\n";
    return 0;
}
