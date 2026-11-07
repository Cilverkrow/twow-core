#include "CorpseRunFirstPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// twow-repo#575 A3 step 4 S4-1 (AiPlayerbot.Revive.CorpseRunFirst): the pure rules of CorpseRunFirstPolicy.h - corpse run
// before the spirit healer, the spirit healer only with a reason, own deaths near the corpse as the loop rule.
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

ai::corpse_first::DeathMark Mark(std::uint32_t at, std::uint32_t map, float x, float y)
{
    ai::corpse_first::DeathMark m;
    m.atSeconds = at;
    m.mapId = map;
    m.x = x;
    m.y = y;
    return m;
}
}

int main()
{
    using namespace ai::corpse_first;

    // 1. Config loaders: an unknown mode is off; MaxSeconds clamped to 120..1800.
    {
        Require(ClampMode(0) == 0 && ClampMode(1) == 1 && ClampMode(2) == 2 && ClampMode(3) == 3, "modes 0-3 pass");
        Require(ClampMode(4) == 0 && ClampMode(-1) == 0 && ClampMode(99) == 0, "an unknown mode acts like 0");
        Require(ClampMaxSeconds(600) == 600, "default max seconds");
        Require(ClampMaxSeconds(10) == MinMaxSeconds && ClampMaxSeconds(-5) == MinMaxSeconds, "max seconds floor");
        Require(ClampMaxSeconds(99999) == MaxMaxSeconds, "max seconds cap");
    }

    // 2. Decide: the corpse run unless a reason applies; the order of the reasons is fixed.
    {
        Input in;
        in.hasCorpse = true;
        in.deadSeconds = 30;
        Require(Decide(in) == Reason::Corpse, "a fresh ghost with a corpse runs to it");

        Input none = in;
        none.hasCorpse = false;
        none.sickness = true;
        Require(Decide(none) == Reason::NoCorpse, "no corpse comes first");

        Input sick = in;
        sick.sickness = true;
        sick.durability = 5;
        Require(Decide(sick) == Reason::Sickness, "resurrection sickness: nothing to lose");

        Input worn = in;
        worn.durability = MinDurability - 1;
        Require(Decide(worn) == Reason::Durability, "durability below 10");
        worn.durability = MinDurability;
        Require(Decide(worn) == Reason::Corpse, "durability 10 still runs");

        Input loop = in;
        loop.deathsNear = LoopDeaths;
        loop.deadSeconds = 99999;
        Require(Decide(loop) == Reason::Loop, "own deaths near the corpse: loop, before the timeout");
        loop.deathsNear = LoopDeaths - 1;
        loop.deadSeconds = 30;
        Require(Decide(loop) == Reason::Corpse, "one death (this one) is no loop");

        Input slow = in;
        slow.deadSeconds = slow.maxSeconds;
        Require(Decide(slow) == Reason::Corpse, "exactly max seconds still runs");
        slow.deadSeconds = slow.maxSeconds + 1;
        Require(Decide(slow) == Reason::Timeout, "longer than max seconds: spirit healer");
    }

    // 3. The legacy "many deaths" reasons are gone: Input has no death count, and a long series far apart stays a corpse run.
    {
        Ring ring;
        for (std::uint32_t i = 0; i < 4; ++i)
            Record(ring, Mark(1000 + i * 100, 0, float(i) * 500.0f, 0.0f));
        Input in;
        in.hasCorpse = true;
        in.deathsNear = DeathsNear(ring, 1300, 0, 1500.0f, 0.0f);
        Require(in.deathsNear == 1, "deaths far apart: only the latest is near");
        Require(Decide(in) == Reason::Corpse, "many deaths at different places: still the corpse run");
    }

    // 4. DeathsNear: window, radius, map; the ring keeps the newest RingSize deaths.
    {
        Ring ring;
        Record(ring, Mark(1000, 0, 0.0f, 0.0f));
        Record(ring, Mark(1200, 0, 30.0f, 40.0f));    // 50 yd away
        Record(ring, Mark(1300, 1, 0.0f, 0.0f));      // other map
        Record(ring, Mark(1400, 0, 100.0f, 0.0f));    // 100 yd away
        Require(ring.count == RingSize, "ring full");
        Require(DeathsNear(ring, 1400, 0, 0.0f, 0.0f) == 2, "two within 60 yd on map 0 inside the window");
        Require(DeathsNear(ring, 1000 + LoopWindowSeconds + 1, 0, 0.0f, 0.0f) == 1, "the oldest leaves the window");
        Require(DeathsNear(ring, 1400, 1, 0.0f, 0.0f) == 1, "map filter");
        Require(DeathsNear(ring, 900, 0, 0.0f, 0.0f) == 0, "deaths after now do not count");
        Record(ring, Mark(1500, 0, 0.0f, 0.0f));      // overwrites the oldest (t=1000)
        Require(ring.count == RingSize, "ring stays at its size");
        Require(DeathsNear(ring, 1500, 0, 0.0f, 0.0f) == 2, "newest kept, oldest dropped");
    }

    // 5. Names for the log lines.
    {
        Require(std::strcmp(ReasonName(Reason::Corpse), "corpse") == 0, "corpse name");
        Require(std::strcmp(ReasonName(Reason::Loop), "loop") == 0, "loop name");
        Require(std::strcmp(ReasonName(Reason::Timeout), "timeout") == 0, "timeout name");
        Require(std::strcmp(ReasonName(Reason::Count), "unknown") == 0, "out of range name");
    }

    // 6. Counters: one minute line per minute.
    {
        Counters counters;
        Require(counters.LogDue(1'800'000'020) && !counters.LogDue(1'800'000'040), "one summary per minute");
        Require(counters.LogDue(1'800'000'080), "the next minute logs again");
        counters.decided[std::size_t(Reason::Loop)].fetch_add(2);
        Require(counters.decided[std::size_t(Reason::Loop)].exchange(0) == 2 && counters.decided[std::size_t(Reason::Loop)].load() == 0,
            "decision counters read and reset");
    }

    std::cout << "corpse_run_first_policy_tests: ok\n";
    return 0;
}
