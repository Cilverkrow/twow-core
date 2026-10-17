// twow-repo#597 (TICK-01): pure parts of AsyncQueries.UnsafeBudgetMs - the stop decision taken before each
// thread-unsafe async callback, the progress guarantee, the kept order across ticks, the [UnsafeBudget]
// counters and line. No server needed (src/shared/Database/UnsafeBudgetPolicy.h).
#include "../src/shared/Database/UnsafeBudgetPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
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

void StopBeforeTest()
{
    using unsafe_budget::StopBefore;
    Require(!StopBefore(0, 0, 0) && !StopBefore(0, 1, 999999999ull) && !StopBefore(0, 5000, ~0ull), "budget 0 = never stop");
    Require(!StopBefore(5, 0, 999999999ull), "the first callback of a pass always runs, however late");
    Require(!StopBefore(5, 1, 4999), "below the budget: go on");
    Require(StopBefore(5, 1, 5000), "at the budget: stop");
    Require(StopBefore(5, 3, 7000), "above the budget: stop");
    Require(!StopBefore(1, 1, 999) && StopBefore(1, 1, 1000), "1 ms budget");
    Require(!StopBefore(0xFFFFFFFFu, 1, 4294967295000ull - 1) && StopBefore(0xFFFFFFFFu, 1, 4294967295000ull), "no overflow at the largest budget");
}

// Mirrors the unsafe loop of SqlResultQueue::Update with a fake clock: the check runs before the next
// callback is taken off the queue, only after one ran and only while one is left; a stop leaves the
// queue untouched.
struct Sim
{
    std::deque<int> queue;
    std::vector<int> ran;
    std::vector<std::uint64_t> costUs;   // per callback id
    std::uint64_t nowUs = 0;
    unsafe_budget::Counters counters;

    // Returns the callbacks run in this pass; capped tells whether the budget stopped it.
    std::uint32_t Pass(std::uint32_t budgetMs, bool& capped)
    {
        capped = false;
        std::uint32_t ranThisPass = 0;
        std::uint64_t const startUs = nowUs;
        for (;;)
        {
            if (budgetMs && ranThisPass && !queue.empty() && unsafe_budget::StopBefore(budgetMs, ranThisPass, nowUs - startUs))
            {
                capped = true;
                break;
            }
            if (queue.empty())
                break;
            int const id = queue.front();
            queue.pop_front();
            nowUs += costUs[id];
            ran.push_back(id);
            ++ranThisPass;
        }
        if (budgetMs && ranThisPass)
            counters.Record(ranThisPass, capped, std::uint32_t(queue.size()), nowUs - startUs);
        return ranThisPass;
    }
};

// Sim holds atomics (not copyable): filled in place, callback ids 0..n-1 in queue order.
void Fill(Sim& sim, std::vector<std::uint64_t> const& costs)
{
    sim.costUs = costs;
    for (int i = 0; i < int(costs.size()); ++i)
        sim.queue.push_back(i);
}

void ZeroBudgetDrainsTest()
{
    Sim sim;
    Fill(sim, { 30000, 30000, 30000, 30000 });
    bool capped = true;
    Require(sim.Pass(0, capped) == 4 && !capped && sim.queue.empty(), "budget 0: the whole queue in one pass (today)");
    unsafe_budget::Snapshot const s = sim.counters.Take();
    Require(s.ran == 0 && s.callsCapped == 0 && s.loopMaxUs == 0, "budget 0: no counters");
}

void FirstCallbackAlwaysRunsTest()
{
    Sim sim;
    Fill(sim, { 50000, 1000, 1000 });   // first callback alone is 10x the budget
    bool capped = false;
    Require(sim.Pass(5, capped) == 1 && capped, "a callback over the budget runs, then the pass stops");
    Require(sim.queue.size() == 2 && sim.queue.front() == 1, "the rest stays queued, next one at the front");
    Require(sim.Pass(5, capped) == 2 && !capped && sim.queue.empty(), "next tick: 2 x 1 ms fit into 5 ms");
}

void StopAtBudgetTest()
{
    // 2 ms each, budget 5 ms: after 3 callbacks 6 ms >= 5 -> stop; after 2 (4 ms) it goes on.
    Sim sim;
    Fill(sim, { 2000, 2000, 2000, 2000, 2000, 2000, 2000 });
    bool capped = false;
    Require(sim.Pass(5, capped) == 3 && capped && sim.queue.size() == 4, "stop once 5 ms are used (3 x 2 ms)");
    // Exactly at the budget: 5 x 1 ms = 5 ms -> stop before the sixth.
    Sim exact;
    Fill(exact, { 1000, 1000, 1000, 1000, 1000, 1000 });
    Require(exact.Pass(5, capped) == 5 && capped && exact.queue.size() == 1, "at exactly the budget the pass stops");
    // Nothing left after the last one: not counted as capped, no clock needed.
    Sim fits;
    Fill(fits, { 3000, 3000 });
    Require(fits.Pass(5, capped) == 2 && !capped, "the queue emptied at the budget: not capped");
}

void OrderKeptTest()
{
    Sim sim;
    Fill(sim, { 4000, 4000, 4000, 4000, 4000, 4000, 4000, 4000, 4000, 4000 });
    bool capped = false;
    int passes = 0;
    while (!sim.queue.empty())
    {
        std::uint32_t const n = sim.Pass(5, capped);
        Require(n >= 1, "progress: at least one callback per pass");
        ++passes;
        Require(passes <= 10, "the queue drains in at most one pass per callback");
    }
    Require(passes == 5, "10 x 4 ms with 5 ms budget: two per pass, five passes");
    for (int i = 0; i < int(sim.ran.size()); ++i)
        Require(sim.ran[i] == i, "callbacks run in queue order across passes");
    Require(sim.ran.size() == 10, "every callback ran exactly once");

    // New callbacks queued between passes go behind the deferred ones.
    Sim late;
    Fill(late, { 6000, 6000, 6000, 1000 });
    late.queue.pop_back();                     // id 3 arrives later
    Require(late.Pass(5, capped) == 1 && capped, "first pass: one 6 ms callback");
    late.queue.push_back(3);
    late.Pass(5, capped);
    late.Pass(5, capped);
    late.Pass(5, capped);
    Require(late.ran.size() == 4 && late.ran[0] == 0 && late.ran[1] == 1 && late.ran[2] == 2 && late.ran[3] == 3,
        "a later callback runs after the deferred ones");
}

void CountersAndLineTest()
{
    unsafe_budget::Counters c;
    c.Record(3, true, 7, 5200);
    c.Record(1, true, 12, 40100);
    c.Record(2, false, 0, 1500);
    unsafe_budget::Snapshot const s = c.Take();
    Require(s.callsCapped == 2 && s.deferredMax == 12 && s.ran == 6 && s.loopMaxUs == 40100, "minute counters");
    unsafe_budget::Snapshot const empty = c.Take();
    Require(empty.callsCapped == 0 && empty.deferredMax == 0 && empty.ran == 0 && empty.loopMaxUs == 0, "Take resets");
    c.Record(4, false, 99, 100);
    Require(c.Take().deferredMax == 0, "deferred_max only from capped passes");

    char buf[160];
    std::size_t const len = unsafe_budget::FormatLine(buf, sizeof(buf), s);
    Require(len == std::strlen(buf), "length returned");
    Require(std::string(buf) == "[UnsafeBudget] calls_capped=2 deferred_max=12 ran=6 loop_max_ms=40.1", "[UnsafeBudget] line");
    char tiny[16];
    std::size_t const cut = unsafe_budget::FormatLine(tiny, sizeof(tiny), s);
    Require(cut == sizeof(tiny) - 1 && tiny[cut] == '\0', "cut at the buffer, terminated");
}
}

int main()
{
    StopBeforeTest();
    ZeroBudgetDrainsTest();
    FirstCallbackAlwaysRunsTest();
    StopAtBudgetTest();
    OrderKeptTest();
    CountersAndLineTest();
    std::cout << "unsafe_budget_597_test: PASS\n";
    return 0;
}
