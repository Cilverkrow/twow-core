// twow-repo#602 ROT-01 failure census (MeleeSkipPolicy.h, OB-00 27.10.2026, Rotation.MeleeSkipAttacking default 1): the default "melee"
// action is USELESS while the bot already attacks its current target, faces it and its pet is on it - where Unit::Attack would fail.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "MeleeSkipPolicy.h"

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
    using namespace ai::melee_skip;

    // 1) Modes: 0 off, 1 every bot, 2 cohort B (odd low guid); unknown values never reach the policy (PlayerbotAIConfig maps them to 1).
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");

    // 2) Skip exactly where Unit::Attack(target, wantMelee) returns false: already the victim, and swinging or not wanting to swing.
    Require(Skip(true, true, true, true, true, true), "melee bot already swinging at its target: skip (warrior in a rage gap)");
    Require(Skip(true, true, false, false, true, true), "ranged bot already attacking its target, no swing wanted: skip (druid in a GCD)");
    Require(!Skip(true, true, false, true, true, true), "attacking but not swinging, swing wanted: attack (Unit::Attack switches to melee)");
    Require(!Skip(true, false, true, true, true, true), "the target is not the victim: attack (a new target)");

    // 3) Side effects of Attack() that still matter keep the old path.
    Require(!Skip(true, true, true, true, false, true), "not facing the target: attack (Attack() turns the bot)");
    Require(!Skip(true, true, true, true, true, false), "living pet not on the target: attack (Attack() sends the pet)");

    // 4) Switch off: never skip (rollback).
    Require(!Skip(false, true, true, true, true, true), "switch off: the old path");

    std::cout << "melee_skip_policy_tests passed\n";
    return 0;
}
