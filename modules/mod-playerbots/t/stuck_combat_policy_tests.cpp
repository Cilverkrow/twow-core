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

    // Hotfix 8.14: home when the bot did not get away from where it was stopped.
    uint32_t const T = 1000 + RescueSeconds;
    State h;
    h.Observe(true, 99999, 1000);
    Require(h.Observe(true, 99999, 1000 + StopSeconds) == Step::Stop, "stop");
    Require(!h.HomeDue(0, -8583.0f, 2546.0f, T), "no stop position yet: no home");
    h.RememberStop(0, -8583.0f, 2546.0f);
    Require(h.Observe(true, 99999, T) == Step::Rescue, "rescue");
    Require(!h.HomeDue(0, -8583.0f + 6.0f, 2546.0f, T), "moved 6 y: no home");
    Require(!h.HomeDue(1, -8583.0f, 2546.0f, T), "other map: no home");
    Require(h.HomeDue(0, -8583.0f + 3.0f, 2546.0f, T), "within 5 y: home");
    Require(!h.HomeDue(0, -8583.0f, 2546.0f, T + 60), "home once per hour");
    h.Observe(false, 0, T + 100);
    Require(!h.stopSet && h.lastHome == T, "leaving combat clears the stop position, keeps the home cooldown");
    h.Observe(true, 99999, T + 5000);
    h.Observe(true, 99999, T + 5000 + StopSeconds);
    h.RememberStop(0, 10.0f, 10.0f);
    Require(!h.HomeDue(0, 10.0f, 10.0f, T + HomeCooldownSeconds - 1), "cooldown still running");
    Require(h.HomeDue(0, 10.0f, 10.0f, T + HomeCooldownSeconds), "home again after an hour");

    std::cout << "stuck_combat_policy_tests passed\n";
    return 0;
}
