// twow-repo#602 ROT-01 #2 (adds analysis 25.10.2026, OB-00): AiPlayerbot.Grind.NeighbourAware - the stage-2 grind pick prefers a
// candidate with fewer neighbours that would join the fight; demote only, snapshot edge, level guard, call-for-help chase.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "GrindNeighbourPolicy.h"

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

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}
}

int main()
{
    using namespace ai::grind_neighbour;

    // Mode: 0 off, 1 all, 2 cohort B (A shadow), 3 shadow only; unknown values act like 0.
    Require(!Computes(0) && Computes(1) && Computes(2) && Computes(3) && !Computes(4), "computed in modes 1-3 only");
    Require(Applies(1, 8) && Applies(1, 9), "mode 1 applies to everybody");
    Require(!Applies(2, 8) && Applies(2, 9), "mode 2 applies to cohort B only");
    Require(!Applies(3, 8) && !Applies(3, 9), "mode 3 applies to nobody (shadow)");
    Require(!Applies(0, 9) && !Applies(7, 9), "off and unknown apply to nobody");

    // Weights in half points and the capped penalty.
    Require(HalfWeight(true, false) == 1 && HalfWeight(false, false) == 2 && HalfWeight(false, true) == 4 && HalfWeight(true, true) == 4,
        "grey 1, normal 2, strong 4");
    Require(Near(Penalty(0), 0.0f) && Near(Penalty(2), 10.0f) && Near(Penalty(12), 60.0f) && Near(Penalty(40), 60.0f),
        "5 yd per half point, at most 60 yd");

    // Replacement: strictly fewer neighbours and a strictly lower score.
    Require(Replaces(30.0f, 0, 20.0f, 4), "a clean mob 10 yd farther beats one with two neighbours (20 + 20 > 30)");
    Require(!Replaces(45.0f, 0, 20.0f, 4), "but not 25 yd farther (20 + 20 < 45)");
    Require(!Replaces(40.0f, 0, 20.0f, 4), "an equal score keeps the distance pick");
    Require(!Replaces(10.0f, 4, 20.0f, 4), "an equal W keeps the distance pick, even nearer");
    Require(!Replaces(10.0f, 6, 20.0f, 4), "more neighbours never replace");
    Require(Replaces(70.0f, 0, 20.0f, 40), "a full pack costs 60 yd at most (20 + 60 > 70)");
    Require(!Replaces(81.0f, 0, 20.0f, 40), "... and no more (20 + 60 < 81)");
    Require(!Replaces(30.0f, 12, 20.0f, 40), "two capped candidates tie on W");

    // Snapshot edge: beyond sight - 30 yd.
    Require(!Edge(45.0f, 75.0f) && Edge(45.5f, 75.0f) && Edge(70.0f, 75.0f), "edge beyond 45 yd at sight 75");

    // Level guard: at most one level above the distance pick, elite only if the pick is.
    Require(GuardOk(12, false, 12, false) && GuardOk(13, false, 12, false) && GuardOk(8, false, 12, false), "up to pick level + 1");
    Require(!GuardOk(14, false, 12, false), "two levels above is refused");
    Require(!GuardOk(12, true, 12, false) && GuardOk(12, true, 12, true), "elite only if the pick is elite");

    // Call for help along the chase: candidate at the origin, bot 30 yd along +x.
    Require(ChaseDistSq(20.0f, 3.0f, 1.0f, 0.0f, 30.0f, 0.0f) < 0.0f, "a melee bot (pull range 0): no chase");
    Require(ChaseDistSq(5.0f, 0.0f, 1.0f, 0.0f, 30.0f, 10.0f) < 0.0f, "a pull range up to 10 yd: no chase part");
    Require(Near(ChaseDistSq(20.0f, 3.0f, 1.0f, 0.0f, 30.0f, 25.0f), 9.0f), "beside the run: perpendicular distance");
    Require(Near(ChaseDistSq(4.0f, 0.0f, 1.0f, 0.0f, 30.0f, 25.0f), 36.0f), "the first 10 yd do not call (nearest point at 10)");
    Require(Near(ChaseDistSq(29.0f, 0.0f, 1.0f, 0.0f, 30.0f, 25.0f), 16.0f), "the run ends at the pull range (25 yd)");
    Require(Near(ChaseDistSq(18.0f, 0.0f, 1.0f, 0.0f, 18.0f, 25.0f), 0.0f), "a bot nearer than its pull range: the run ends at the bot");
    Require(Within(9.0f, 3.0f) && !Within(9.1f, 3.0f) && !Within(0.0f, 0.0f), "within a radius; radius 0 = nothing");

    // Counters start at zero.
    Require(counters[0].passes.load() == 0 && counters[1].changed.load() == 0 && classPicks[1][3].load() == 0, "counters start at 0");

    std::cout << "grind_neighbour_policy tests passed\n";
    return 0;
}
