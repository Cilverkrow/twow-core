// A3 step 2 quest book source choice and order (QuestSourcePolicy.h, AiPlayerbot.QuestBook.SourceChoice). Hand-rolled Require(),
// std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "QuestSourcePolicy.h"

#include <cmath>
#include <cstdint>
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

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}
}

int main()
{
    using namespace ai::questsource;
    Params const p;   // killSec 12, singleMargin 1.5, minSpeed 1

    // 1) Kills for a need at a drop chance.
    {
        Require(Kills(0, 40) == 0, "nothing needed");
        Require(Kills(10, 100) == 10, "a kill objective");
        Require(Kills(60, 40) == 150, "case 899: 60 tusks at 40 % = 150 kills");
        Require(Kills(1, 30) == 4, "rounded up");
        Require(Kills(5, 0) == 5 && Kills(5, 200) == 5, "unknown or broken chance counts as 100");
    }

    // 2) Work at the source.
    {
        Stats s;
        s.spawns = 20;
        s.respawnSec = 300;
        s.spacingYd = 35;
        Require(Near(WorkSec(10, s, 7.0f, p), 10.0f * (12.0f + 5.0f)), "10 kills, 12 s + 35 yd / 7 yd/s each");
        Require(WorkSec(0, s, 7.0f, p) == 0.0f, "no need, no work");
        Stats one;
        one.spawns = 1;
        one.respawnSec = 600;
        Require(Near(WorkSec(3, one, 7.0f, p), 1200.0f), "one spawn, 3 kills: two respawn waits bound the work");
        Stats unknown;
        unknown.respawnSec = 600;
        Require(Near(WorkSec(3, unknown, 7.0f, p), 36.0f), "unknown spawn count: no respawn bound");
        Require(Near(WorkSec(1, s, 0.0f, p), 12.0f + 35.0f), "zero speed is floored at 1 yd/s");
        Params noKill;
        noKill.killSec = -5.0f;
        Require(Near(WorkSec(1, s, 7.0f, noKill), 5.0f), "a negative kill time counts as 0");
        Stats vendor;
        vendor.spawns = 1;
        vendor.respawnSec = 300;
        vendor.vendor = true;
        Require(Near(WorkSec(10, vendor, 7.0f, p), 12.0f) && WorkSec(0, vendor, 7.0f, p) == 0.0f,
            "a vendor is one visit: no kills, no respawn bound");
    }

    // 3) Route.
    {
        Require(Near(RouteSec(700.0f, 7.0f, p), 100.0f), "700 yd at 7 yd/s");
        Require(RouteSec(-3.0f, 7.0f, p) == 0.0f, "no negative route");
    }

    // 4) The pick (cases 47 Goldtooth and 1003 Den Mother: a named source against a common one).
    {
        Option o[3];
        o[0] = {400.0f, false, true};   // the common kobolds
        o[1] = {300.0f, true, true};    // the named one, only 1.33x cheaper
        Require(PickSource(o, 2, p) == 0, "a named source must beat the common one by the margin");
        o[1].cost = 250.0f;             // 1.6x cheaper
        Require(PickSource(o, 2, p) == 1, "by the margin it wins");
        o[0].valid = false;
        Require(PickSource(o, 2, p) == 1, "only a named source left");
        o[1].valid = false;
        Require(PickSource(o, 2, p) == -1, "nothing valid");
        o[0] = {500.0f, false, true};
        o[1] = {450.0f, false, true};
        o[2] = {450.0f, false, true};
        Require(PickSource(o, 3, p) == 1, "the cheapest common source; a tie keeps the earlier one");
        Require(PickSource(nullptr, 3, p) == -1 && PickSource(o, 0, p) == -1, "no options");
        Params loose;
        loose.singleMargin = 0.5f;      // below 1 acts as 1
        o[2] = {440.0f, true, true};
        Require(PickSource(o, 3, loose) == 2, "a margin below 1 is 1: the cheaper named source wins");
    }

    // 5) Side XP and the order score.
    {
        Require(Near(SideXp(150, 40), 6000.0f), "150 kills at 40 xp");
        float const near = Score(1000.0f, 0.0f, 0.0f, 60.0f, 240.0f);
        Require(Near(near, 200.0f), "1000 xp in 5 min = 200 xp/min");
        Require(Score(1000.0f, 500.0f, 0.0f, 60.0f, 240.0f) > near, "a follow-up quest raises the score");
        Require(Score(1000.0f, 0.0f, 500.0f, 60.0f, 240.0f) > near, "side xp raises the score");
        Require(Score(1000.0f, 0.0f, 0.0f, 600.0f, 240.0f) < near, "a long route lowers it");
        Require(Near(Score(10.0f, 0.0f, 0.0f, 0.0f, 0.0f), 600.0f), "a zero time is floored at one second");
        Require(Score(-5.0f, 0.0f, -5.0f, 60.0f, 60.0f) == 0.0f, "negative xp counts as 0");
    }

    std::cout << "questsource_policy_tests: all passed\n";
    return 0;
}
