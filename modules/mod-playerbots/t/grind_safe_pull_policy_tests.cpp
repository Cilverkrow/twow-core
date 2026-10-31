// twow-repo#656 BOT-PULL-01-L1 (#653 lever 1, OB-00 31.10.2026): AiPlayerbot.Grind.SafePull - the stage-2 grind pick orders the
// candidates by the creatures that would join the fight (linked 2, sight at target / fight spot / approach 1), the snapshot edge is
// never safe, the readiness wait is bounded, and the death line counts the attackers of the counted set.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "SafePullPolicy.h"

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
    using namespace ai::safe_pull;

    // Mode: 0 off, 1 all, 2 cohort B, 3 cohort A (crossover; the other cohort in shadow), 4 shadow only; unknown values act like 0.
    Require(!Computes(0) && Computes(1) && Computes(2) && Computes(3) && Computes(4) && !Computes(5), "computed in modes 1-4 only");
    Require(Applies(1, 8) && Applies(1, 9), "mode 1 applies to everybody");
    Require(!Applies(2, 8) && Applies(2, 9), "mode 2 applies to cohort B (odd low guid) only");
    Require(Applies(3, 8) && !Applies(3, 9), "mode 3 applies to cohort A (even low guid) only");
    Require(!Applies(4, 8) && !Applies(4, 9), "mode 4 applies to nobody (shadow)");
    Require(!Applies(0, 9) && !Applies(7, 9), "off and unknown apply to nobody");

    // Weights and the cap.
    Require(LinkWeight == 2 && SightWeight == 1, "linked 2, sight 1");
    Require(Add(0, LinkWeight) == 2 && Add(15, LinkWeight) == RiskCap && Add(RiskCap, SightWeight) == RiskCap, "risk capped at 16");

    // The fight spot: melee at the candidate; ranged SpotBack yd inside the pull range, never beyond the bot.
    Require(Near(SpotOffset(40.0f, 0.0f), 0.0f), "melee: at the candidate");
    Require(Near(SpotOffset(40.0f, 26.0f), 21.0f), "ranged: pull range - 5");
    Require(Near(SpotOffset(12.0f, 26.0f), 12.0f), "a bot nearer than its spot: the spot is the bot");
    Require(Near(SpotOffset(40.0f, 4.0f), 0.0f), "a pull range below SpotBack: at the candidate");
    Require(Near(SpotOffset(0.0f, 26.0f), 0.0f), "bot on the candidate: offset 0");

    // The approach line (bot at the origin, spot 30 yd along +x).
    Require(Near(SegmentDistSq(15.0f, 4.0f, 0.0f, 0.0f, 30.0f, 0.0f), 16.0f), "beside the line: perpendicular distance");
    Require(Near(SegmentDistSq(-3.0f, 4.0f, 0.0f, 0.0f, 30.0f, 0.0f), 25.0f), "behind the bot: distance to the bot");
    Require(Near(SegmentDistSq(36.0f, 8.0f, 0.0f, 0.0f, 30.0f, 0.0f), 100.0f), "beyond the spot: distance to the spot");
    Require(Near(SegmentDistSq(3.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f), 25.0f), "a zero-length line: distance to the point");

    // Snapshot edge: never safe.
    Require(EdgeRisk(0, true) == 1 && EdgeRisk(3, true) == 3 && EdgeRisk(0, false) == 0, "edge: risk at least 1");
    Require(Edge(46.0f, 75.0f) && !Edge(45.0f, 75.0f), "edge = beyond sight - 30");

    // Order: lower risk first, then the lower score distance.
    Require(Better(0, 50.0f, 1, 10.0f), "a safe far candidate beats a risky near one");
    Require(Better(1, 10.0f, 1, 20.0f) && !Better(1, 20.0f, 1, 10.0f) && !Better(1, 10.0f, 1, 10.0f), "same risk: nearer wins, a tie keeps");
    Require(!Better(2, 5.0f, 1, 60.0f), "higher risk never wins");
    // The level guard is the neighbour rule's.
    Require(GuardOk(11, false, 10, false) && !GuardOk(12, false, 10, false) && !GuardOk(10, true, 10, false) && GuardOk(10, true, 10, true),
        "at most +1 level, elite only if the distance pick is");

    // Readiness: risk >= 2 needs HP and (mana users) mana; the wait ends after MaxWaitS.
    Require(Ready(80, true, 50, 80, 50) && !Ready(79, true, 90, 80, 50) && !Ready(95, true, 49, 80, 50), "HP and mana thresholds");
    Require(Ready(80, false, 0, 80, 50), "a rage / energy user needs no mana");
    Require(!Waits(1, false, 0), "risk 1 never waits");
    Require(Waits(2, false, 0) && Waits(5, false, MaxWaitS - 1) && !Waits(2, false, MaxWaitS), "risk >= 2 waits at most MaxWaitS");
    Require(!Waits(4, true, 0), "a ready bot never waits");

    // The death line: attackers in the counted set.
    LastPick last;
    last.counted = 3;
    last.guids[0] = 101;
    last.guids[1] = 102;
    last.guids[2] = 103;
    std::uint64_t const attackers[] = { 102, 200, 103, 0 };
    Require(CountedAmong(last, attackers, 4) == 2, "two of the attackers were counted");
    Require(CountedAmong(LastPick(), attackers, 4) == 0, "an empty pick counts nothing");
    Require(MaxCounted == 8 && DeathWindowS == 300 && WaitRisk == 2 && MaxWaitS == 90, "constants");

    // Counters start at zero.
    Require(counters[0].passes.load() == 0 && counters[1].waits.load() == 0 && classPicks[1][3].load() == 0, "counters start at 0");

    std::cout << "grind_safe_pull_policy tests passed\n";
    return 0;
}
