// twow-repo#602 ROT-01 REF-next v2 tick (LosCachePolicy.h, OB-00 29.10.2026, Rotation.LosCacheMs default 500, 0 = rollback): a bot keeps
// one line-of-sight verdict per target; the CastLosGate cast checks and the reach prerequisite reuse it.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "LosCachePolicy.h"

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
    using namespace ai::los_cache;

    // 1) The switch: default 500 ms, bound 5000 (the read maps larger values to the default).
    Require(DefaultMs == 500 && MaxMs == 5000, "default 500, bound 5000");

    // 2) Fresh: younger than the switch's ms; 0 = off, never fresh.
    Require(Fresh(500, 0) && Fresh(500, 499), "a verdict younger than 500 ms is fresh");
    Require(!Fresh(500, 500) && !Fresh(500, 4000), "500 ms and older: stale");
    Require(!Fresh(0, 0) && !Fresh(0, 100), "switch off (0): never fresh (the old path queries every time)");

    // 3) The gate skips the CheckCast only for a fresh, out-of-sight verdict on the same target ...
    Require(SkipCast(true, true, false, false), "fresh, same target, out of sight: IMPOSSIBLE without CheckCast");
    Require(!SkipCast(false, true, false, false), "stale verdict: the full check");
    Require(!SkipCast(true, false, false, false), "another target: the full check");
    Require(!SkipCast(true, true, true, false), "the target is in sight: the full check (sight is not the problem)");

    // 4) ... and never for a spell with SPELL_ATTR_EX2_IGNORE_LOS (or with an unknown entry, treated the same: the old path decides).
    Require(!SkipCast(true, true, false, true), "a spell that ignores line of sight always takes the full check");

    // 5) Reach answers "in sight and in range" first: not useful whatever CanCastSpell says.
    Require(ReachInPlace(true, true, 25.0f, 30.0f) && ReachInPlace(true, true, 30.0f, 30.0f), "in sight, within range: not useful");
    Require(!ReachInPlace(true, true, 30.5f, 30.0f), "in sight but out of range: the old path (CanCastSpell, then move)");
    Require(!ReachInPlace(true, false, 10.0f, 30.0f), "out of sight: the old path (force a move)");
    Require(!ReachInPlace(false, true, 10.0f, 30.0f), "switch off: the old order");

    // 6) The counters start at 0 (the [CastLosGate] minute line reports increments).
    Require(cached.load() == 0 && reachEarly.load() == 0, "counters start at 0");

    std::cout << "los_cache_policy_tests passed\n";
    return 0;
}
