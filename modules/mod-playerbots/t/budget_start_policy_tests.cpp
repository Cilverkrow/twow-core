// twow-repo#591 (PERF-AB-01): bots without a travel target choose one without budget; a configurable per-window limit.
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

    // Maribel (235), L1, right after login: no travel target - the choice skips the budget.
    Require(StartExempt(true, "choose travel target", false), "no target: exempt");
    Require(StartExempt(true, "request quest travel target", false) && StartExempt(true, "choose group travel target", false),
        "the other target choices too");
    Require(!StartExempt(true, "choose travel target", true), "negative: a bot with a target waits as before");
    Require(!StartExempt(true, "attack anything", false) && !StartExempt(true, "choose rpg target", false),
        "negative: other expensive actions stay budgeted");
    Require(!StartExempt(false, "choose travel target", false), "switch off: as before");

    // Limit: 0 = the mode's, otherwise the configured one.
    Require(LimitFor(1, 0) == 3 && LimitFor(2, 0) == 1, "mode limits unchanged");
    Require(LimitFor(1, 8) == 8 && LimitFor(2, 8) == 8, "configured limit");
    ThreadBudget budget;
    int taken = 0;
    for (int i = 0; i < 10; ++i)
        taken += TryTake(1, 1, 100, 100, 0, budget, 8) ? 1 : 0;
    Require(taken == 8, "eight per window with limit 8");
    budget = ThreadBudget{};
    taken = 0;
    for (int i = 0; i < 10; ++i)
        taken += TryTake(1, 1, 200, 200, 0, budget) ? 1 : 0;
    Require(taken == 3, "default call keeps mode 1 at three");

    std::cout << "budget_start_policy_tests passed\n";
    return 0;
}
