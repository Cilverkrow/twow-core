#include "UnreachablePolicy.h"

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
    using namespace ai::unreachable;

    // Stuck: the distance stays at 25 yards, one attempt per second.
    Approach stuck;
    bool marked = false;
    for (uint32_t t = 100; t <= 120 && !marked; ++t)
        marked = stuck.Update(1, 25.f, t);
    Require(marked, "no progress for 20 s: unreachable");

    // Closing in: every attempt gains a yard or more - never unreachable.
    Approach closing;
    bool closingMarked = false;
    for (uint32_t t = 0; t < 60; ++t)
        closingMarked |= closing.Update(2, 80.f - float(t) * 1.2f, t);
    Require(!closingMarked, "closing in is never unreachable");

    // A pause longer than GapSeconds starts a new window (a fight in between).
    Approach paused;
    for (uint32_t t = 0; t < 15; ++t)
        paused.Update(3, 30.f, t);
    Require(!paused.Update(3, 30.f, 15 + GapSeconds + 1), "a new window after a pause");
    Require(!paused.Update(3, 30.f, 30), "still inside the new window");

    // Another target starts over.
    Require(!stuck.Update(9, 25.f, 200), "new target, new window");

    // Ignore list: five minutes, bounded.
    IgnoreList list;
    list.Add(1, 1000);
    Require(list.Ignored(1, 1000 + IgnoreSeconds - 1) && !list.Ignored(1, 1000 + IgnoreSeconds), "ignored for five minutes");
    for (uint64_t g = 10; g < 10 + MaxIgnored + 5; ++g)
        list.Add(g, 2000 + uint32_t(g));
    Require(list.until.size() <= MaxIgnored, "bounded");
    Require(!list.Ignored(10, 2100) && list.Ignored(10 + MaxIgnored + 4, 2100), "oldest dropped first");

    std::cout << "unreachable_policy_tests passed\n";
    return 0;
}
