// twow-repo#602 ROT-01 (L1-b, OB-10): AiPlayerbot.CastWait.Preparing - hold the internal AI until the real end of the
// bot's own PREPARING cast, with the OB-10 guards (deadline once per spell, never shorten, release at the end).
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "CastWaitPolicy.h"

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
    using namespace ai::cast_wait;
    std::uint32_t const react = 100, diff = 50;

    // 1. Cap: twice the cast time, at least cast time + 1.5 s.
    Require(CapMs(1500) == 3000 && CapMs(1000) == 2500 && CapMs(3500) == 7000, "cap = max(2 x cast, cast + 1500)");
    Require(CapMs(0) == 1500 && CapMs(-5) == 1500, "no cast time: 1.5 s");

    // 2. Wrapping clock.
    Require(Until(100, 150) == 50 && Until(150, 100) == 0, "until");
    Require(Until(0xFFFFFF00u, 0x00000040u) == 0x140, "until across the wrap");

    // 3. First sighting: the deadline is fixed once; the wait is the real rest + margins.
    Watch w;
    Require(!Active(w), "empty watch");
    Grant g = Extend(w, 0xA0, 1000, 1500, 1500, react, diff, 0);
    Require(Active(w) && g.firstSighting && !g.cappedNow, "first sighting");
    Require(w.deadline == 1000 + 3000 && w.otherEnd == 1000, "deadline once from the first sighting, nothing pending");
    Require(g.delay == 1500 + react + diff, "wait = m_timer + ReactDelay + avg diff");

    // 4. Pushback: the same spell later with more time left keeps its deadline; the deadline cuts the wait (counted once).
    g = Extend(w, 0xA0, 2700, 1400, 1500, react, diff, 0);
    Require(!g.firstSighting && w.deadline == 4000, "same spell: same deadline");
    Require(g.delay == 1300 && g.cappedNow && w.capped, "capped by the deadline (4000 - 2700)");
    g = Extend(w, 0xA0, 3900, 900, 1500, react, diff, 0);
    Require(g.delay == 100 && !g.cappedNow, "capped again, counted once");
    g = Extend(w, 0xA0, 4100, 500, 1500, react, diff, 0);
    Require(g.delay == 0, "deadline passed: no wait, polling as before");

    // 5. Never shorten a longer delay someone else set.
    Watch v;
    g = Extend(v, 0xB0, 1000, 500, 2000, react, diff, 2000);
    Require(g.delay == 2000 && v.otherEnd == 3000, "an existing longer delay is kept");

    // 6. A new spell (other object, or same address with another cast time) is a new sighting.
    g = Extend(w, 0xC0, 5000, 2000, 2000, react, diff, 0);
    Require(g.firstSighting && w.deadline == 5000 + 4000 && !w.capped, "new spell: new deadline");
    g = Extend(w, 0xC0, 5100, 1000, 1000, react, diff, 0);
    Require(g.firstSighting && w.deadline == 5100 + 2500, "reused address, other cast time: new sighting");

    // 7. Release: the spell stopped PREPARING, vanished or changed.
    Watch r;
    Extend(r, 0xD0, 1000, 1500, 1500, react, diff, 0);
    Require(!ShouldRelease(r, 0xD0, 1500, true), "still preparing: keep");
    Require(ShouldRelease(r, 0xD0, 1500, false), "cast or channel started: release");
    Require(ShouldRelease(r, 0, 0, false), "no spell any more (interrupted, cancelled): release");
    Require(ShouldRelease(r, 0xE0, 1500, true), "another spell: release");
    Require(!ShouldRelease(Watch(), 0, 0, false), "no wait: nothing to release");

    // 8. Released delay: back to what was pending (here nothing); a delay set after the grant is kept.
    Require(ReleasedDelay(r, 1200, 1450) == 0, "own wait cut to the pending delay (none)");
    Watch p;
    Extend(p, 0xF0, 1000, 500, 1500, react, diff, 300);   // 300 ms were pending, the wait is 650 ms
    Require(p.otherEnd == 1300 && p.grantEnd == 1650, "pending end and grant end");
    Require(ReleasedDelay(p, 1100, 550) == 200, "release keeps the 200 ms still pending from before");
    Require(ReleasedDelay(p, 1100, 3000) == 3000, "a longer delay set after the grant is kept");
    Clear(p);
    Require(!Active(p), "cleared");

    // 9. Saved passes (estimate).
    Require(SavedTicks(1650, 100) == 16 && SavedTicks(50, 100) == 0 && SavedTicks(500, 0) == 0, "saved ticks = wait / ReactDelay");

    std::cout << "cast_wait_policy_tests passed\n";
    return 0;
}
