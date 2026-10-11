// twow-repo#541 A47 (AiPlayerbot.BotSave.IntervalSeconds): spread first saves, due rule, held core timer, per-tick limit.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>

#include "BotSavePolicy.h"

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
    using namespace ai::bot_save;

    Require(FirstSaveOffset(1234, 0) == 0, "off: no offset");
    std::set<std::uint32_t> offsets;
    for (std::uint32_t guid = 1000; guid < 1300; ++guid)
    {
        std::uint32_t const offset = FirstSaveOffset(guid, 300);
        Require(offset < 300, "offset inside the interval");
        offsets.insert(offset);
    }
    Require(offsets.size() == 300, "300 consecutive guids spread over all 300 seconds");

    Require(!Due(1000, 900, 0), "off: never due");
    Require(!Due(1000, 900, 300), "not yet");
    Require(Due(1200, 900, 300), "due at the interval");
    Require(Due(5000, 900, 300), "due after it");
    Require(!Due(800, 900, 300), "clock before the last save: not due");

    Require(HoldCoreTimerMs(300) == 360000 && HoldCoreTimerMs(60) > 60000, "core timer held beyond the own interval");

    ThreadBudget budget;
    Require(TryTake(1000, 1, budget) && !TryTake(1000, 1, budget), "one save per tick");
    Require(TryTake(1050, 1, budget), "next tick: next save");
    budget = ThreadBudget{};
    Require(TryTake(2000, 2, budget) && TryTake(2000, 2, budget) && !TryTake(2000, 2, budget), "two per tick");
    Require(TryTake(3000, 0, budget) && TryTake(3000, 0, budget) && TryTake(3000, 0, budget), "0 = no limit");

    // Live v46.1: the window must move with the clock. A frozen key (tickTime() == 0 in this core) allows one save ever.
    Require(BudgetWindow(0) == BudgetWindow(BudgetWindowMs - 1) && BudgetWindow(BudgetWindowMs) == BudgetWindow(0) + 1,
        "100 ms windows");
    budget = ThreadBudget{};
    int saved = 0;
    for (std::uint32_t nowMs = 0; nowMs < 1000; nowMs += 10)
        saved += TryTake(BudgetWindow(nowMs), 1, budget) ? 1 : 0;
    Require(saved == 1000 / BudgetWindowMs, "one save per window, every window");
    budget = ThreadBudget{};
    saved = 0;
    for (int i = 0; i < 100; ++i)
        saved += TryTake(0, 1, budget) ? 1 : 0;
    Require(saved == 1, "negative: a frozen key never resets");

    std::cout << "bot_save_policy_tests passed\n";
    return 0;
}
