// twow-repo#541 (AiPlayerbot.Perf.ActionBudget): limits per thread and tick, spread slots, no starvation.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "ActionBudgetPolicy.h"

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
    using namespace ai::action_budget;

    ThreadBudget budget;
    Require(TryTake(0, 1, 1000, 20, 0, budget) && TryTake(0, 1, 1000, 20, 0, budget), "mode 0: always");

    // Mode 1: three per tick, a new tick refills.
    budget = ThreadBudget{};
    Require(TryTake(1, 1, 1000, 20, 0, budget) && TryTake(1, 2, 1000, 20, 0, budget) && TryTake(1, 3, 1000, 20, 0, budget),
        "mode 1: three in one tick");
    Require(!TryTake(1, 4, 1000, 20, 0, budget), "mode 1: the fourth waits");
    Require(TryTake(1, 4, 1050, 21, 0, budget), "mode 1: next tick refills");

    // Mode 2: one per tick.
    budget = ThreadBudget{};
    Require(TryTake(2, 1, 2000, 40, 0, budget) && !TryTake(2, 2, 2000, 40, 0, budget), "mode 2: one per tick");
    Require(TryTake(2, 2, 2050, 41, 0, budget), "mode 2: next tick");

    // Mode 3: each bot in its own slot of SpreadSlots ticks; over SpreadSlots ticks every bot gets exactly one.
    budget = ThreadBudget{};
    for (std::uint32_t bot = 1; bot <= 8; ++bot)
    {
        int allowed = 0;
        for (std::uint32_t tick = 100; tick < 100 + SpreadSlots; ++tick)
            allowed += TryTake(3, bot, tick * 50, tick, 0, budget) ? 1 : 0;
        Require(allowed == 1, "mode 3: one slot per bot in each window");
    }

    // No starvation: after MaxDeferrals deferrals in a row the action runs whatever the budget says.
    budget = ThreadBudget{};
    Require(TryTake(2, 1, 3000, 60, 0, budget), "take the only one");
    Require(!TryTake(2, 2, 3000, 60, MaxDeferrals - 1, budget), "still waits below the limit");
    Require(TryTake(2, 2, 3000, 60, MaxDeferrals, budget), "forced at MaxDeferrals");
    Require(TryTake(3, 1, 0, 1, MaxDeferrals, budget), "mode 3 forced at MaxDeferrals too");

    std::vector<std::string> const expensive = DefaultExpensiveActions();
    Require(IsExpensive("choose travel target", expensive) && IsExpensive("find corpse", expensive) &&
        IsExpensive("add gathering loot", expensive), "owner's list");
    Require(!IsExpensive("melee", expensive) && !IsExpensive("check values", expensive), "cheap actions not limited");
    Require(LimitFor(1) == 3 && LimitFor(2) == 1 && LimitFor(0) == 0 && LimitFor(3) == 0, "limits");

    std::cout << "action_budget_policy_tests passed\n";
    return 0;
}
