// twow-repo#602 ROT-01 failure census v3 (NeverPossiblePolicy.h, OB-00 28.10.2026, Rotation.NeverPossibleSkip default 1): a spell action whose
// spell the bot does not know skips its checks and its reach prerequisite; actions with runtime spell names keep them.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "NeverPossiblePolicy.h"

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
    using namespace ai::never_possible;

    // 1) Modes: 0 off, 1 every bot, 2 cohort B (odd low guid); unknown values never reach the policy (PlayerbotAIConfig maps them to 1).
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");

    // 2) Skip only an unknown spell (Cone of Cold below L26: 11 % of all action time in hunt2).
    Require(Skip(true, false, 0), "unknown spell: skip");
    Require(!Skip(true, false, 120), "known spell (id 120): the full checks");

    // 3) A learned spell is seen as soon as the "spell id" value has it: the same input decides, no cached "never".
    Require(Skip(true, false, 0) && !Skip(true, false, 10230), "before / after learning: skip, then checks");

    // 4) Runtime / placeholder names keep the old path (shoot, pull, paladin blessings, shaman imbue, vehicle spells, mount).
    Require(!Skip(true, true, 0), "keeps unknown spell: the full checks");

    // 5) Switch off: never skip (rollback).
    Require(!Skip(false, false, 0), "switch off: the old path");

    std::cout << "never_possible_policy_tests passed\n";
    return 0;
}
