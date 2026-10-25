// twow-repo#581 (QUEST-LOOP-01): QL-1 failure memory per bot x quest with a window and a cooldown, QL-2 minimum cooldown.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "QuestLoopPolicy.h"

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
    using namespace ai::quest_loop;
    std::uint32_t const minute = 60000, window = 30 * minute, cooldown = 30 * minute;

    // Ysmara-like loop: an inactive drop about every 3 minutes - the 3rd suppresses for 30 min.
    Record r;
    std::uint32_t const t = 1000000;
    Require(!RecordFailure(r, t, 3, window, cooldown) && !RecordFailure(r, t + 3 * minute, 3, window, cooldown),
        "two failures: still allowed");
    Require(RecordFailure(r, t + 6 * minute, 3, window, cooldown), "third failure in the window: suppressed");
    Require(IsSuppressed(r, t + 6 * minute + 1) && IsSuppressed(r, t + 35 * minute), "skipped for 30 minutes");
    Require(!IsSuppressed(r, t + 37 * minute), "back after the cooldown");
    Require(!RecordFailure(r, t + 10 * minute, 3, window, cooldown), "failures during the cooldown do not extend it");
    Require(IsSuppressed(r, t + 35 * minute), "cooldown unchanged by them");

    // After the cooldown the count starts again.
    Require(!RecordFailure(r, t + 40 * minute, 3, window, cooldown) && !RecordFailure(r, t + 41 * minute, 3, window, cooldown),
        "fresh count after the cooldown");

    // Negative: failures far apart (normal contention) never suppress.
    Record slow;
    bool suppressed = false;
    for (std::uint32_t i = 0; i < 10; ++i)
        suppressed = RecordFailure(slow, t + i * 20 * minute, 3, window, cooldown) || suppressed;
    Require(!suppressed, "one failure every 20 minutes: never suppressed");

    // Off.
    Record off;
    for (std::uint32_t i = 0; i < 10; ++i)
        Require(!RecordFailure(off, t + i, 0, window, cooldown), "MaxFailures 0: off");
    Require(!IsSuppressed(off, t), "off: never suppressed");

    // Clock wrap: a suppression across the 32-bit wrap still holds.
    Record wrap;
    std::uint32_t const nearWrap = 0xFFFFFFFFu - minute;
    Require(RecordFailure(wrap, nearWrap, 1, window, cooldown), "one failure with MaxFailures 1");
    Require(IsSuppressed(wrap, nearWrap + 2 * minute), "suppressed across the clock wrap");

    // QL-2: at least the configured seconds, never shorter than the destination's own cooldown.
    Require(InactiveCooldownMs(60000, 180) == 180000, "60 s -> 180 s");
    Require(InactiveCooldownMs(300000, 180) == 300000, "a longer destination cooldown stays");
    Require(InactiveCooldownMs(60000, 0) == 60000, "0 = as before");

    // InactiveAfterProgress (twow-repo#601): only a grown count is progress; no count never is.
    Require(InactiveAfterProgress(0, 1), "0 -> 1 kill: progress");
    Require(InactiveAfterProgress(4, 7), "4 -> 7 items: progress");
    Require(!InactiveAfterProgress(3, 3), "no new credit: an emptied camp as before");
    Require(!InactiveAfterProgress(5, 2), "a smaller count (items handed in or destroyed): no progress");
    Require(!InactiveAfterProgress(-1, 4), "no count at the start: rule as before");
    Require(!InactiveAfterProgress(2, -1), "quest gone from the log: rule as before");
    Require(ProgressCooldownMs < InactiveCooldownMs(60000, 0), "the progress cooldown is shorter than the destination's");

    std::cout << "quest_loop_policy_tests passed\n";
    return 0;
}
