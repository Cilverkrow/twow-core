#include "WorldBotsTracePolicy.h"

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
    using namespace ai::world_bots;

    Window w;
    Require(!w.Due(600), "first call opens the window");
    Require(w.minute == 10, "window minute");
    w.Add(800, 1000, 3000, 500, 1, 2);
    w.Add(800, 2000, 1000, 0, 0, 1);
    Require(w.passes == 2 && w.bots == 1600, "passes and bots summed");
    Require(w.sessionsUs == 3000 && w.processUs == 4000 && w.otherUs == 500, "parts summed");
    Require(w.maxPassUs == 4500, "slowest pass kept");
    Require(w.logins == 1 && w.teleportAcks == 3, "counts summed");
    Require(!w.Due(659), "same minute: not due");
    Require(w.Due(660), "next minute: due");

    // 7.5 ms busy in a minute = 0.125 per mille, rounded down; 60 ms = 1 per mille.
    Require(w.PerMille() == 0, "small share rounds down");
    Window busy;
    busy.Due(0);
    busy.Add(800, 30000, 30000, 0, 0, 0);
    Require(busy.PerMille() == 1, "60 ms per minute = 1 per mille");
    busy.Add(800, 6000000, 0, 0, 0, 0);
    Require(busy.PerMille() == 101, "6.06 s per minute = 101 per mille");

    w.Reset(660);
    Require(w.passes == 0 && w.maxPassUs == 0 && w.minute == 11, "reset opens the new minute");

    std::cout << "world_bots_trace_policy_tests passed\n";
    return 0;
}
