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

    std::cout << "loot_hold_policy_tests: OK\n";
    return 0;
}
