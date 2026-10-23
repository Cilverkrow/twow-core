// twow-repo#594 (AiPlayerbot.Rotation.ReachLosClose): ReachLosPolicy.h.
#include <cstdlib>
#include <iostream>

#include "ReachLosPolicy.h"

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
    return a - b < 0.001f && b - a < 0.001f;
}
}

int main()
{
    using namespace ai::reach_los;
    float const preferred = 30.0f * 0.75f;   // 30 yd spell range
    float const tooClose = 5.0f;

    // In sight: always 75 % of the range, mode or not, hostile or friendly.
    Require(Near(ChaseDistance(true, false, false, 20.7f, preferred, tooClose).distance, preferred), "in LOS, hostile, off");
    Require(Near(ChaseDistance(true, false, true, 20.7f, preferred, tooClose).distance, preferred), "in LOS, hostile, on");
    Require(Near(ChaseDistance(true, true, true, 20.7f, preferred, tooClose).distance, preferred), "in LOS, friend, on (CLI-ROT gap)");
    Require(ChaseDistance(true, false, true, 20.7f, preferred, tooClose).kind == Kind::Unchanged, "in LOS: unchanged");
    // Friendly out of sight: unchanged 0.9 rule, mode or not.
    Require(Near(ChaseDistance(false, true, false, 20.0f, preferred, tooClose).distance, 18.0f), "friend out of LOS: x 0.9");
    Require(Near(ChaseDistance(false, true, true, 20.0f, preferred, tooClose).distance, 18.0f), "friend out of LOS, on: unchanged");
    // The Linalella case: hostile, 20.7 yd, out of sight.
    Require(Near(ChaseDistance(false, false, false, 20.7f, preferred, tooClose).distance, preferred), "off: 22.5 yd - never moves");
    Chase const on = ChaseDistance(false, false, true, 20.7f, preferred, tooClose);
    Require(Near(on.distance, 20.7f * 0.5f) && on.kind == Kind::Closed, "on: half the distance, closed");
    // Far and out of sight: capped at preferred.
    Require(Near(ChaseDistance(false, false, true, 100.0f, preferred, tooClose).distance, preferred), "on, far: capped at preferred");
    // The floor: a caster without min range never closer than tooCloseDistance; a hunter never inside its dead zone.
    Chase const nearCaster = ChaseDistance(false, false, true, 6.0f, preferred, Floor(0.0f, tooClose));
    Require(Near(nearCaster.distance, 5.0f) && nearCaster.kind == Kind::Floored, "caster at 6 yd: floored at 5 (not 3)");
    Chase const hunter = ChaseDistance(false, false, true, 12.0f, preferred, Floor(8.0f, tooClose));
    Require(Near(hunter.distance, 8.0f) && hunter.kind == Kind::Floored, "hunter at 12 yd: floored at its 8 yd min range");
    // d near 0 (CLI-ROT gap): the floor holds, no negative or zero chase.
    Chase const zero = ChaseDistance(false, false, true, 0.0f, preferred, Floor(0.0f, tooClose));
    Require(Near(zero.distance, 5.0f) && zero.kind == Kind::Floored, "d = 0: floored");
    // A floor above preferred (tiny-range spell) never pushes the bot out beyond preferred.
    Require(Near(ChaseDistance(false, false, true, 3.0f, 4.0f, 8.0f).distance, 4.0f), "floor capped at preferred");
    Require(Near(Floor(8.0f, tooClose), 8.0f) && Near(Floor(0.0f, tooClose), 5.0f), "floor source");

    // Mode: 0 off, 1 all, 2 cohort B (odd guid); unknown acts like off.
    Require(!Applies(0, 5) && !Applies(0, 4), "mode 0: nobody");
    Require(Applies(1, 5) && Applies(1, 4), "mode 1: everybody");
    Require(Applies(2, 5) && !Applies(2, 4), "mode 2: odd guid only");
    Require(!Applies(7, 5), "unknown mode: off");

    Require(FormatMinuteLine(2, 0, 12, 3, 4) == "[ReachLos] minute mode=2 closed_A=0 closed_B=12 floored=3 in_los_after=4", "minute line");

    std::cout << "reach_los_tests passed\n";
    return 0;
}
