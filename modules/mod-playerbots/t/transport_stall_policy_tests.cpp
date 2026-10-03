#include "TransportStallPolicy.h"

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
    using namespace ai::transport_stall;
    uint32_t const min = 60 * 1000;

    // Talaster (#497): waiting on the zeppelin platform, short gathering trips in between.
    State s;
    Require(s.Observe(3185, 40001, true, false, 2169.0f, 1000) == Step::None, "observation starts");
    Require(s.Observe(3185, 40001, true, false, 2260.0f, 1000 + 3 * min) == Step::None, "gathering trip away is no progress");
    Require(s.Observe(3185, 40001, true, false, 2169.0f, 1000 + 5 * min) == Step::Wait, "5 minutes: wait line");
    Require(s.minutes == 5, "wait minutes");
    Require(s.Observe(3185, 40001, true, false, 2170.0f, 1000 + 6 * min) == Step::None, "no second wait line within 5 minutes");
    Require(s.Observe(3185, 40001, true, false, 2169.0f, 1000 + 10 * min) == Step::Wait, "10 minutes: next wait line");
    Require(s.Observe(3185, 40001, true, false, 2169.0f, 1000 + 12 * min) == Step::Abandon, "12 minutes without a better distance: abandon");
    Require(s.minutes == 12, "abandon minutes");
    Require(s.Observe(3185, 40001, true, false, 2169.0f, 1000 + 13 * min) == Step::None, "after abandon a fresh observation");

    // Walking to the dock shortens the distance through the map transfer: progress.
    State w;
    w.Observe(3185, 40001, true, false, 4000.0f, 0);
    for (uint32_t m = 1; m <= 20; ++m)
        Require(w.Observe(3185, 40001, true, false, 4000.0f - m * 100.0f, m * min) == Step::None, "walking to the dock never stalls");

    // Pauses (combat, aboard the zeppelin, ...) restart the window.
    State p;
    p.Observe(3185, 40001, true, false, 2169.0f, 0);
    p.Observe(3185, 40001, true, true, 2169.0f, 11 * min);
    Require(p.Observe(3185, 40001, true, false, 2169.0f, 20 * min) == Step::Wait, "pause restarted the window");
    Require(p.Observe(3185, 40001, true, false, 2169.0f, 23 * min) == Step::Abandon, "12 minutes after the pause");

    // Same map: not observed. Another target: fresh window.
    State m;
    Require(m.Observe(1, 1, false, false, 50.0f, 0) == Step::None, "same map");
    Require(m.Observe(1, 1, false, false, 50.0f, 30 * min) == Step::None, "same map never abandons here");
    m.Observe(1, 1, true, false, 2000.0f, 30 * min);
    Require(m.Observe(2, 7, true, false, 2000.0f, 50 * min) == Step::None, "another target starts fresh");

    std::cout << "transport_stall_policy_tests passed\n";
    return 0;
}
