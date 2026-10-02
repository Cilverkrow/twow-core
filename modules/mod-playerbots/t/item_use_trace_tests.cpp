#include "ItemUseTrace.h"

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
    using namespace ai::item_use;

    Trace t;
    Require(!t.Due(100), "nothing used, no line");
    t.OnUse(true, true, 100);
    t.OnOpen(110);
    Require(t.uses == 1 && t.started == 1 && t.bandages == 1 && t.opens == 1, "counters");
    Require(!t.Due(100 + 3599) && t.Due(100 + 3600), "a line after one hour");
    for (int i = 0; i < 18; ++i)
        t.OnUse(false, false, 200);
    Require(t.Due(200), "the first line after 20 events");
    t.Reset();
    Require(t.uses == 0 && t.opens == 0 && !t.Due(9000), "reset");
    for (int i = 0; i < 25; ++i)
        t.OnUse(false, false, 10000);
    Require(!t.Due(10000) && t.Due(10000 + 3600), "after the first line only hourly");

    std::cout << "item_use_trace_tests passed\n";
    return 0;
}
