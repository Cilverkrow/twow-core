#include "CombatIdlePolicy.h"

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

// twow-repo#541 (audit A17): pure policy of AiPlayerbot.Perf.CombatIdleYield. The integration (engine result,
// real Unit state, the wake path) is pinned by t/combat_idle_yield_source_contract_tests.cmake.
int main()
{
    using namespace ai::combat_idle;

    // Counters start at zero.
    Require(stretched.load() == 0 && woken.load() == 0, "counters start at zero");

    // Delay: 3x ReactDelay; with CanUpdateAIInternal (< 100) the next pass needs > 200 ms elapsed.
    Require(StretchedDelay(100) == 300, "3x the default ReactDelay");
    Require(StretchedDelay(0) == 0, "ReactDelay 0 stays 0");
    Require(StretchedDelay(100) - 100 >= 200, "at least 200 ms between idle passes at ReactDelay 100");

    // Cheap preconditions.
    Require(MayStretch(true, true, true, false), "all preconditions hold");
    Require(!MayStretch(false, true, true, false), "switch off -> never");
    Require(!MayStretch(true, false, true, false), "an action ran or not the combat engine -> no stretch");
    Require(!MayStretch(true, true, false, false), "a longer delay was already set (cast, action duration) -> keep it");
    Require(!MayStretch(true, true, true, true), "minimal yield (out of combat) -> unchanged");

    // Auto attack.
    Require(IsAutoAttacking(true, true, false), "melee swing at the current target in reach");
    Require(IsAutoAttacking(true, false, true), "auto shot / wand at the current target");
    Require(!IsAutoAttacking(true, false, false), "victim set but no swing in reach (caster with a stale melee state, chase)");
    Require(!IsAutoAttacking(false, true, true), "swinging at something other than the current target");

    // Wake.
    Require(!ShouldWake(0, 0, false, false), "nothing pending -> no wake");
    Require(!ShouldWake(0, 42, false, false), "nothing pending, other victim -> no wake");
    Require(!ShouldWake(42, 42, true, false), "same victim, still swinging in reach");
    Require(!ShouldWake(42, 42, false, true), "same victim, auto shot running");
    Require(ShouldWake(42, 0, false, false), "victim gone (died, evaded, AttackStop)");
    Require(ShouldWake(42, 43, true, false), "victim changed");
    Require(ShouldWake(42, 43, false, true), "victim changed while auto shot runs");
    Require(ShouldWake(42, 42, false, false), "auto attack stopped or victim left melee reach");

    // Wake only cuts a delay up to the stretched wait.
    Require(WakeResets(250, 300), "pending stretched wait is cut");
    Require(WakeResets(300, 300), "full stretched wait is cut");
    Require(!WakeResets(0, 300), "already due -> nothing to cut, no wake counted");
    Require(!WakeResets(301, 300), "a delay longer than the stretch is kept");
    Require(!WakeResets(1500, 300), "a longer delay set by someone else (teleport) is kept");

    std::cout << "combat_idle policy tests passed\n";
    return 0;
}
