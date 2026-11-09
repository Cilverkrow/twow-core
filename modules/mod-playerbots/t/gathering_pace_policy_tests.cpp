#include "GatheringPacePolicy.h"
#include "GatheringTracePolicy.h"
#include "CohortPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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
    using namespace ai::gathering_pace;

    // Cohort modes as for the other crossover keys: 2 = odd guids (B), 3 = even guids (A).
    Require(ai::cohort::Applies(2, 7) && !ai::cohort::Applies(2, 8), "mode 2: cohort B only");
    Require(ai::cohort::Applies(3, 8) && !ai::cohort::Applies(3, 7), "mode 3: cohort A only");
    Require(!ai::cohort::Applies(0, 7) && !ai::cohort::Applies(0, 8), "mode 0: nobody");

    // The reach: 30-60 yd, else the default 45.
    Require(Yards(45) == 45 && Yards(30) == 30 && Yards(60) == 60, "reach inside the range kept");
    Require(Yards(29) == DefaultYards && Yards(61) == DefaultYards && Yards(0) == DefaultYards, "reach outside -> 45");
    Require(!YardsValid(25) && YardsValid(30) && !YardsValid(1000), "reach range");

    // Skills: herbalism and mining only.
    Require(IsPaceSkill(182) && IsPaceSkill(186), "herbalism and mining");
    Require(!IsPaceSkill(393), "no skinning");
    Require(!IsPaceSkill(185) && !IsPaceSkill(142) && !IsPaceSkill(356), "no cooking, survival, fishing (W2)");
    Require(!IsPaceSkill(129) && !IsPaceSkill(633) && !IsPaceSkill(0), "no first aid, lockpicking, none");

    // Lag rule: below min(rank cap, 5 x level).
    Require(Behind(22, 75, 85), "22 at cap 75, level 17: behind");
    Require(!Behind(75, 75, 85), "at the rank cap: not behind");
    Require(Behind(49, 150, 50) && !Behind(50, 150, 50), "the 5 x level limit");
    Require(!Behind(0, 0, 50), "not learned: never behind");

    // Colour: orange or yellow only (value < req + 50).
    Require(FarColorOk(1, 50) && !FarColorOk(1, 51), "req 1: 50 yellow, 51 green");
    Require(FarColorOk(75, 124) && !FarColorOk(75, 125), "req 75: the +50 edge");

    // Strong hostile: level >= bot level - 7.
    Require(Strong(20, 13) && !Strong(20, 12), "the 7-level gap");
    Require(Strong(5, 1), "low levels: no wrap");

    State s;
    uint32_t const t0 = 100000;
    uint64_t const node = 0xF110000000001234ull;
    uint64_t const other = 0xF110000000005678ull;

    // A fresh node is eligible; the hostile cache starts unknown.
    Require(s.FarOk(node, t0), "fresh node eligible");
    Require(!s.FarOk(0, t0), "no guid, no far pick");
    Require(s.HostilesClear(node, t0) == -1, "hostiles unknown");
    s.PutHostiles(node, t0, true);
    Require(s.HostilesClear(node, t0 + HostileCacheSeconds - 1) == 1, "hostiles cached for 10 s");
    Require(s.HostilesClear(node, t0 + HostileCacheSeconds) == -1, "hostile cache expires after 10 s");
    s.PutHostiles(node, t0 + 20, false);
    Require(s.HostilesClear(node, t0 + 20) == 0, "a strong hostile cached");
    Require(s.HostilesClear(node, t0 + 20 + HostileCacheSeconds) == -1, "a strong hostile also expires after 10 s");

    // The pick starts the clock; a re-pick of the same node does not restart it or count again.
    State fresh;
    Require(fresh.BudgetLeft(t0) && fresh.PicksLastHour(t0) == 0, "no far pick yet: budget left");
    Require(s.Pick(node, t0), "first far pick counts");
    Require(!s.Pick(node, t0 + 5), "re-pick: no second count");
    Require(s.pickedAt == t0 && s.PicksLastHour(t0 + 5) == 1, "clock from the first pick");
    Require(s.FarOk(node, t0 + TimeoutSeconds - 1), "eligible until the timeout");
    Require(!s.EndDue(node, t0 + TimeoutSeconds - 1), "no timeout at 19 s");
    Require(!s.EndDue(other, t0 + 60), "timeout only for the pending pick");
    Require(!s.FarOk(node, t0 + TimeoutSeconds), "a timed-out pick is not eligible");

    // Timeout: 20 s after the pick -> the caller ends it: miss list for 600 s, pending gone.
    Require(s.EndDue(node, t0 + TimeoutSeconds), "timeout due at 20 s after the pick");
    s.End(node, t0 + TimeoutSeconds);
    Require(s.pending == 0 && s.Missed(node, t0 + TimeoutSeconds), "missed after the timeout");
    Require(!s.EndDue(node, t0 + TimeoutSeconds + 1), "ended: nothing pending");
    Require(!s.FarOk(node, t0 + TimeoutSeconds + MissSeconds - 1), "missed for 600 s");
    Require(s.FarOk(node, t0 + TimeoutSeconds + MissSeconds), "eligible again after 600 s");

    // Opened: ends the clock, no miss.
    Require(s.Pick(other, t0 + 100), "second far pick");
    Require(!s.Opened(node), "open of another node: no effect");
    Require(s.Opened(other) && s.pending == 0 && !s.Missed(other, t0 + 200), "open ends the far pick");

    // Leave: a near pick lets a pending far pick go; a timed-out one goes on the miss list.
    Require(s.Pick(node, t0 + 2000), "far pick");
    Require(!s.Leave(t0 + 2005) && s.pending == 0 && !s.Missed(node, t0 + 2005), "left before 20 s: no miss");
    Require(s.Pick(node, t0 + 3000), "far pick again");
    Require(s.Leave(t0 + 3000 + TimeoutSeconds) && s.Missed(node, t0 + 3000 + TimeoutSeconds), "left after 20 s: missed");
    Require(!s.Leave(t0 + 3100), "nothing pending");

    // A strong hostile seen near the pending node (cached check) ends the far pick before the 20 s; a clear node does not.
    State h;
    Require(h.Pick(node, t0), "far pick (hostile case)");
    h.PutHostiles(node, t0 + 5, true);
    Require(!h.EndDue(node, t0 + 5), "clear node: no end");
    h.PutHostiles(node, t0 + 6, false);
    Require(h.EndDue(node, t0 + 6) && !h.EndDue(other, t0 + 6), "a strong hostile near the pending node ends it");
    h.End(node, t0 + 6);
    Require(h.pending == 0 && h.Missed(node, t0 + 6), "hostile end: missed");

    // Budget: at most 12 new far picks in any 3600 s (sliding); the pending pick stays eligible at the cap.
    State b;
    uint32_t const h0 = 500000;
    for (uint32_t i = 1; i <= FarPicksPerHour; ++i)
        Require(b.FarOk(i, h0 + i) && b.Pick(i, h0 + i), "picks within the budget");
    Require(b.PicksLastHour(h0 + 100) == FarPicksPerHour, "12 picks in the last hour");
    Require(!b.FarOk(100, h0 + 100), "13th node: no budget");
    Require(b.FarOk(FarPicksPerHour, h0 + 13), "the pending pick stays eligible at the cap");
    Require(!b.FarOk(100, h0 + HourSeconds), "still 12 picks within the last 3600 s");
    Require(b.FarOk(100, h0 + 1 + HourSeconds), "the oldest pick left the hour: one slot");
    Require(b.Pick(100, h0 + 1 + HourSeconds) && b.PicksLastHour(h0 + 1 + HourSeconds) == FarPicksPerHour, "sliding: 12 per hour");
    Require(!b.FarOk(101, h0 + 1 + HourSeconds), "no second slot before the next oldest pick leaves the hour");
    Require(b.FarOk(101, h0 + 2 + HourSeconds), "the next oldest left: one slot");

    // Miss list: 8 slots, the oldest is overwritten.
    State m;
    for (uint64_t g = 1; g <= MissSlots; ++g)
        m.AddMiss(g, h0);
    Require(m.Missed(1, h0) && m.Missed(MissSlots, h0), "8 misses held");
    m.AddMiss(MissSlots + 1, h0);
    Require(!m.Missed(1, h0) && m.Missed(2, h0) && m.Missed(MissSlots + 1, h0), "9th miss overwrites the oldest");
    m.AddMiss(2, h0 + 300);
    Require(m.Missed(2, h0 + 300 + MissSeconds - 1), "a re-miss renews its 600 s");

    // The [Gathering] event=share window counts far picks, opens and timeouts only while open; appended at the line end.
    ai::gathering_trace::Window w;
    w.NoteFar(ai::gathering_trace::FarEvent::Pick);
    Require(w.farPicks == 0, "no window: nothing counted");
    w.Sample(1000, false, false);
    w.NoteFar(ai::gathering_trace::FarEvent::Pick);
    w.NoteFar(ai::gathering_trace::FarEvent::Pick);
    w.NoteFar(ai::gathering_trace::FarEvent::Open);
    w.NoteFar(ai::gathering_trace::FarEvent::Timeout);
    Require(w.farPicks == 2 && w.farOpens == 1 && w.farTimeouts == 1, "far counters");
    std::string const line = ai::gathering_trace::ShareLine(7, 15, w, {}, {}, {});
    std::string const tail = " s393=0/0 far_picks=2 far_opens=1 far_timeouts=1";
    Require(line.size() > tail.size() && line.compare(line.size() - tail.size(), tail.size(), tail) == 0,
        "far counters appended after s393");
    w.Roll();
    Require(w.farPicks == 0 && w.farOpens == 0 && w.farTimeouts == 0, "far counters are hourly increments");
    return 0;
}
