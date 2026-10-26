// twow-repo#602 ROT-01 (c) (hunter + adds analysis 25.10.2026, OB-00): AiPlayerbot.Rotation.FleeAway - the flee chase angle relative to
// the enemy's facing, a kept chase of the same enemy for ReissueMs, the hunter dead zone while the pet holds the enemy.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "FleeAwayPolicy.h"

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
    return std::fabs(a - b) < 0.0001f;
}
}

int main()
{
    using namespace ai::flee_away;

    // Mode: 0 off, 1 all, 2 cohort B (odd low guid); unknown values act like 0.
    Require(!Applies(0, 8) && !Applies(0, 9), "mode 0 applies to nobody");
    Require(Applies(1, 8) && Applies(1, 9), "mode 1 applies to everybody");
    Require(!Applies(2, 8) && Applies(2, 9), "mode 2 applies to cohort B only");
    Require(!Applies(3, 9), "an unknown mode applies to nobody");

    // Scope (26.10.): 0 every bot, 1 hunters only, 2 hunters and bots with their pet out; unknown values act like 0.
    Require(InScope(0, false, false) && InScope(0, true, true), "scope 0: every bot");
    Require(InScope(1, true, false) && InScope(1, true, true), "scope 1: hunters with or without a pet");
    Require(!InScope(1, false, false) && !InScope(1, false, true), "scope 1: no other class, pet or not");
    Require(InScope(2, true, false) && InScope(2, false, true), "scope 2: hunters and bots with their pet out");
    Require(!InScope(2, false, false), "scope 2: no petless non-hunter");
    Require(InScope(3, false, false), "an unknown scope: every bot, as before");

    // (a) The core places the chase point at enemy facing + angle: with the relative angle that is the enemy-to-bot direction.
    float const toBot = 1.0f, facing = 2.5f;
    float const rel = RelativeAngle(toBot, facing);
    Require(rel >= 0.0f && rel < 2.0f * Pi, "normalised to [0, 2 pi)");
    float placed = facing + rel;
    while (placed >= 2.0f * Pi)
        placed -= 2.0f * Pi;
    Require(Near(placed, toBot), "facing + relative angle = the direction from the enemy to the bot");
    Require(Near(RelativeAngle(2.0f, 2.0f), 0.0f), "an enemy facing the bot: straight ahead of it");
    Require(Near(RelativeAngle(0.5f, 6.0f), 0.5f - 6.0f + 2.0f * Pi), "negative difference wraps");
    Require(Near(RelativeAngle(6.2f, 0.1f), 6.1f), "large difference stays below 2 pi");

    // (b) Keep the same enemy's running chase for ReissueMs, then re-aim; never keep a different enemy or a stopped chase.
    Require(KeepChase(true, true, 0) && KeepChase(true, true, ReissueMs - 1), "kept inside ReissueMs");
    Require(!KeepChase(true, true, ReissueMs), "re-aimed after ReissueMs");
    Require(!KeepChase(true, false, 10), "another enemy: new chase");
    Require(!KeepChase(false, true, 10), "no chase running: new chase");

    // (c) The dead zone: 8 yd + 1 yd + the combat reach of the trigger's distance.
    Require(InDeadZone(9.0f, 0.0f) && !InDeadZone(9.1f, 0.0f), "9 yd without reach");
    Require(InDeadZone(11.5f, 2.5f) && !InDeadZone(12.0f, 2.5f), "plus the combat reach");

    // Counters start at zero.
    Require(counters[0].issued.load() == 0 && counters[1].deadZoneQuiet.load() == 0 && counters[1].outOfScope.load() == 0,
        "counters start at 0");

    std::cout << "flee_away_policy tests passed\n";
    return 0;
}
