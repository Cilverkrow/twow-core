#include "TankPathDiagPolicy.h"

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
    using namespace ai::tank_path_diag;

    Window window;
    Require(!window.Due(1000), "no window before the first restart");
    window.Restart(1000);
    Require(!window.Due(1000 + SummarySeconds - 1) && window.Due(1000 + SummarySeconds), "a summary every ten minutes");

    window.Sample(false, false);
    Require(window.pulls == 0 && window.combatSamples == 0, "out of combat: nothing counted");
    window.Sample(true, true);
    window.Sample(true, true);
    window.Sample(true, false);
    window.Sample(true, true);
    Require(window.pulls == 1, "one pull per combat");
    Require(window.combatSamples == 4 && window.AggroPercent() == 75, "aggro share of the combat samples");
    window.Sample(false, false);
    window.Sample(true, false);
    Require(window.pulls == 2, "a new combat is a new pull");

    window.taunts = 3;
    window.deaths = 1;
    window.Restart(1600);
    Require(window.start == 1600 && window.taunts == 0 && window.deaths == 0 && window.pulls == 0, "restart clears the counters");
    window.Sample(true, true);
    Require(window.pulls == 0, "a fight running across the restart is no new pull");

    Window empty;
    Require(empty.AggroPercent() == 0, "no combat, no share");

    std::cout << "tank_path_diag_policy_tests passed\n";
    return 0;
}
