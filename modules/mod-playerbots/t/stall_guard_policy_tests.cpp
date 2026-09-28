#include "StallGuardPolicy.h"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

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
    using namespace ai::stall_guard;

    // Rate limit: the first slow update is logged, then at most once a minute.
    Require(ShouldLog(0, 1000, SlowUpdateLogSeconds), "first slow update is logged");
    Require(!ShouldLog(1000, 1059, SlowUpdateLogSeconds), "no second line within the minute");
    Require(ShouldLog(1000, 1060, SlowUpdateLogSeconds), "next line after the minute");

    // Budget: generous; a normal route (milliseconds) is never cut.
    Require(!BudgetExceeded(50, RouteBudgetMs), "a normal route stays within the budget");
    Require(!BudgetExceeded(RouteBudgetMs, RouteBudgetMs), "exactly the budget is still allowed");
    Require(BudgetExceeded(RouteBudgetMs + 1, RouteBudgetMs), "over the budget stops the search");
    Require(BudgetExceeded(21146, RouteBudgetMs), "the #416 stall length is far over the budget");
    Require(!BudgetExceeded(99999, 0), "budget 0 = no budget");

    // Keys: per bot and target cell (100 yards), maps kept apart (Turtle maps > 255).
    Require(RouteCooldownStore::Key(7, 0, 10.0f, 10.0f) == RouteCooldownStore::Key(7, 0, 90.0f, 60.0f), "same cell, same key");
    Require(RouteCooldownStore::Key(7, 0, 10.0f, 10.0f) != RouteCooldownStore::Key(7, 0, 150.0f, 10.0f), "next cell, other key");
    Require(RouteCooldownStore::Key(7, 0, 10.0f, 10.0f) != RouteCooldownStore::Key(8, 0, 10.0f, 10.0f), "other bot, other key");
    Require(RouteCooldownStore::Key(7, 1, 10.0f, 10.0f) != RouteCooldownStore::Key(7, 257, 10.0f, 10.0f), "maps 1 and 257 differ");
    Require(RouteCooldownStore::Key(7, 0, -9500.0f, 1200.0f) != RouteCooldownStore::Key(7, 0, 9500.0f, 1200.0f), "negative coordinates differ");

    // Cooldown after an exceeded budget: no retry storm for that bot and cell.
    RouteCooldownStore store;
    std::uint64_t const key = RouteCooldownStore::Key(291, 0, 1500.0f, -9300.0f);
    Require(!store.IsBlocked(key, 100), "no cooldown before a failure");
    store.Block(key, 100 + RouteCooldownSeconds, 100);
    Require(store.IsBlocked(key, 101), "blocked right after the failure");
    Require(store.IsBlocked(key, 100 + RouteCooldownSeconds - 1), "blocked during the cooldown");
    Require(!store.IsBlocked(key, 100 + RouteCooldownSeconds), "free after the cooldown");
    Require(!store.IsBlocked(RouteCooldownStore::Key(292, 0, 1500.0f, -9300.0f), 101), "other bots are not blocked");

    // Bounded, also under concurrent use from several map threads.
    RouteCooldownStore shared;
    std::vector<std::thread> threads;
    for (std::uint32_t t = 0; t < 4; ++t)
        threads.emplace_back([&shared, t]() {
            for (std::uint32_t i = 0; i < 20000; ++i)
            {
                std::uint64_t const k = RouteCooldownStore::Key(t * 100000 + i, 0, float(i), 0.0f);
                shared.Block(k, 1000000, i / 10);
                shared.IsBlocked(k, i / 10);
            }
        });
    for (std::thread& thread : threads)
        thread.join();
    Require(shared.Size() <= RouteCooldownStore::MaxEntries, "cooldown store stays bounded");

    std::cout << "stall_guard_policy_tests passed\n";
    return 0;
}
