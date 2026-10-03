#include "StuckCombatPolicy.h"

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
    using namespace ai::stuck_combat;

    State s;
    Require(s.Observe(false, 99999, 100) == Step::None, "out of combat: nothing");
    Require(s.Observe(true, 99999, 1000) == Step::None, "combat starts");
    Require(s.Observe(true, 99999, 1000 + StopSeconds - 1) == Step::None, "under 10 minutes");
    Require(s.Observe(true, 99999, 1000 + StopSeconds) == Step::Stop, "10 minutes without progress: stop once");
    Require(s.Observe(true, 99999, 1000 + StopSeconds + 60) == Step::None, "no second stop");
    Require(s.Observe(true, 99999, 1000 + RescueSeconds) == Step::Rescue, "20 minutes: rescue allowed");
    Require(s.Observe(true, 99999, 1000 + RescueSeconds + 60) == Step::Rescue, "and stays allowed while stuck");
    Require(s.Minutes(1000 + RescueSeconds) == RescueSeconds / 60, "minutes in combat");

    State p;
    p.Observe(true, 0, 2000);
    Require(p.Observe(true, 30, 2000 + StopSeconds + 300) == Step::None, "a long fight with progress is no stuck combat");

    s.Observe(false, 0, 9000);
    Require(s.since == 0 && !s.stopped, "leaving combat resets");

    std::cout << "stuck_combat_policy_tests passed\n";
    return 0;
}
