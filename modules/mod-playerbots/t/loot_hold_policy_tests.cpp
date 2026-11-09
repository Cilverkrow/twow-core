#include "LootHoldPolicy.h"

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
    using namespace ai::loot_hold;

    // Key off (0 = as before): never a hold.
    Require(!Hold(false, false), "key off, no loot target: no hold");
    Require(!Hold(false, true), "key off, open loot target: no hold (as before)");
    // Key on: a hold only with an open loot target.
    Require(!Hold(true, false), "key on, no open loot target: no hold");
    Require(Hold(true, true), "key on, open loot target: hold");

    // Loot distance bound (default AiPlayerbot.LootDistance 25), inclusive.
    Require(WithinLootDistance(0.0f, 25.0f), "at the object");
    Require(WithinLootDistance(24.9f, 25.0f), "inside the loot distance");
    Require(WithinLootDistance(25.0f, 25.0f), "on the loot distance (inclusive)");
    Require(!WithinLootDistance(25.1f, 25.0f), "beyond the loot distance: the bot left the object behind, no hold");
    Require(!WithinLootDistance(500.0f, 25.0f), "far away: no hold");
    Require(!WithinLootDistance(-1.0f, 25.0f), "a negative distance is no distance");

    // Hold limit per loot target (OB-10 review 09.11.): clamp 10..600, default 60.
    Require(DefaultMaxSeconds == 60, "default 60 s");
    Require(ClampMaxSeconds(60) == 60 && ClampMaxSeconds(10) == 10 && ClampMaxSeconds(600) == 600, "in range kept");
    Require(ClampMaxSeconds(0) == 10 && ClampMaxSeconds(-5) == 10 && ClampMaxSeconds(9) == 10, "below 10 -> 10");
    Require(ClampMaxSeconds(601) == 600 && ClampMaxSeconds(100000) == 600, "above 600 -> 600");

    // The clock: a new guid starts it, the same guid keeps it below the limit, expires once at the limit, stays expired.
    std::uint64_t const a = 0xF110000000123456ull, b = 0xF130000000654321ull;
    Require(Next(a, 0, 0, 1000, 60) == Step::Start, "first loot target: start");
    Require(Next(a, b, 900, 1000, 60) == Step::Start, "another loot target: start again");
    Require(Next(a, a, 1000, 1000, 60) == Step::Keep, "same target, 0 s: keep");
    Require(Next(a, a, 1000, 1059, 60) == Step::Keep, "same target, 59 s: keep");
    Require(Next(a, a, 1000, 1060, 60) == Step::Expire, "same target, 60 s: expire");
    Require(Next(a, a, 1000, 5000, 60) == Step::Expire, "same target, long after: expire");
    Require(Next(a, a, 0, 5000, 60) == Step::Expired, "same target after its expiry: expired, no second line");
    Require(Next(b, a, 0, 5000, 60) == Step::Start, "a new target after an expiry: start");
    Require(Holds(Step::Start) && Holds(Step::Keep), "start and keep hold");
    Require(!Holds(Step::Expire) && !Holds(Step::Expired), "expire and expired do not hold (no hold without end)");

    std::cout << "loot_hold_policy_tests: OK\n";
    return 0;
}
