// twow-repo#602 ROT-01 failure census (InvalidTargetPolicy.h, OB-00 28.10.2026, Rotation.InvalidTargetQuiet default 1): "invalid target" stays
// quiet for an empty current target while nothing attacks the bot; the retarget to the next add and a non-empty invalid target fire as before.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "InvalidTargetPolicy.h"

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
    using namespace ai::invalid_target;

    // 1) Modes: 0 off, 1 every bot, 2 cohort B (odd low guid); unknown values never reach the policy (PlayerbotAIConfig maps them to 1).
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");

    // 2) Quiet only for an empty target without attackers (hunt2: Zexnik 1,274 "select new target" FAILED in a row in 6 min).
    Require(Quiet(true, true, false), "empty target, no attackers: quiet");
    Require(!Quiet(true, true, true), "empty target with attackers: fires (retarget to the next add)");
    Require(!Quiet(true, false, false) && !Quiet(true, false, true), "a non-empty target: the old check decides");

    // 3) Switch off: never quiet (rollback).
    Require(!Quiet(false, true, false), "switch off: the old trigger");

    std::cout << "invalid_target_policy_tests passed\n";
    return 0;
}
