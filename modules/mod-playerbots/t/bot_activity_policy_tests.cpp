// twow-repo#601 R01 L1 (AiPlayerbot.ActivityTrace): BotActivityPolicy.h - kinds, the per-bot clock, the lines and the
// minute counters.
#include "BotActivityPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
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
}

int main()
{
    using namespace ai::bot_activity;

    // Bands: L0-4, L5-9, L10-14 (the R01 bands), L60 and above in the last one.
    Require(BandOf(0) == 0 && BandOf(4) == 0 && BandOf(5) == 1, "L0-4 band 0, L5 band 1");
    Require(BandOf(10) == 2 && BandOf(14) == 2 && BandOf(15) == 3 && BandOf(24) == 4, "R01 bands 10-14 / 15-19 / 20-24");
    Require(BandOf(59) == 11 && BandOf(60) == Bands - 1 && BandOf(63) == Bands - 1, "L60 and above in the last band");

    // Travel status: PREPARE..WORK active, NONE / COOLDOWN / EXPIRED not.
    Require(!TravelActive(0) && TravelActive(1) && TravelActive(2) && TravelActive(3) && TravelActive(4), "1-4 active");
    Require(!TravelActive(5) && !TravelActive(6) && !TravelActive(7), "cooldown, expired and above not active");

    // Kinds: dead before combat before the target before a flight; idle is what is left.
    Require(Classify(false, true, true, Dest::Quest, true) == Kind::Dead, "dead wins");
    Require(Classify(true, true, true, Dest::Quest, false) == Kind::Combat, "combat before the target");
    Require(Classify(true, false, true, Dest::Quest, false) == Kind::Quest, "active quest target");
    Require(Classify(true, false, true, Dest::Grind, false) == Kind::Grind, "active grind target");
    Require(Classify(true, false, true, Dest::Other, false) == Kind::Other, "active other target");
    Require(Classify(true, false, true, Dest::None, false) == Kind::Idle, "active status without a destination is idle");
    Require(Classify(true, false, false, Dest::Quest, false) == Kind::Idle, "quest target in cooldown is idle");
    Require(Classify(true, false, false, Dest::None, true) == Kind::Other, "taxi flight without target is other");
    Require(Classify(true, false, true, Dest::Quest, true) == Kind::Quest, "taxi flight to a quest target is quest");
    Require(Classify(true, false, false, Dest::None, false) == Kind::Idle, "nothing is idle");

    // Clock: the first sample credits nothing, later ones the seconds since the previous one; a long gap is gap.
    {
        Clock c;
        std::uint32_t const t0 = 3600u * 500000u + 100u;
        Require(c.Due(t0), "first sample due");
        Require(c.Add(Kind::Idle, t0) == 0 && c.hour == 500000u, "first sample credits nothing, sets the hour");
        Require(!c.Due(t0 + 59) && c.Due(t0 + 60), "next sample after 60 s");
        Require(c.Add(Kind::Quest, t0 + 60) == 60 && c.seconds[std::size_t(Kind::Quest)] == 60, "60 s to quest");
        Require(c.Add(Kind::Combat, t0 + 150) == 90 && c.seconds[std::size_t(Kind::Combat)] == 90, "90 s to combat");
        Require(c.Add(Kind::Idle, t0 + 271) == 0 && c.gap == 121 && c.seconds[std::size_t(Kind::Idle)] == 0, "121 s is a gap");
        Require(c.Add(Kind::Idle, t0 + 391) == 120 && c.seconds[std::size_t(Kind::Idle)] == 120, "120 s still credited");
        Require(!c.HourDone(t0 + 391), "same hour");
        // the next hour: Add first (seconds still go to the old hour), then the line and a fresh hour
        std::uint32_t const t1 = 3600u * 500001u + 10u;
        Require(c.Add(Kind::Grind, t1) == 0 && c.HourDone(t1), "long gap into the next hour, hour done");
        std::string const line = FormatLine(12, 14, 40, c);
        Require(line == "[BotActivity] bot=12 level=14 zone=40 hour=500000 quest=60 grind=0 combat=90 other=0 idle=120 dead=0 gap=3240",
            "hour line");
        c.StartHour(t1);
        Require(c.hour == 500001u && c.gap == 0 && c.seconds[std::size_t(Kind::Quest)] == 0, "fresh hour");
        Require(c.Add(Kind::Grind, t1 + 60) == 60 && !c.HourDone(t1 + 60), "credit in the new hour");
    }

    // Counters and the minute line: samples and seconds per band, empty bands left out, Take resets.
    {
        Counters counters;
        counters.Add(BandOf(12), Kind::Quest, 60);
        counters.Add(BandOf(13), Kind::Idle, 60);
        counters.Add(BandOf(60), Kind::Dead, 30);
        counters.Add(Bands, Kind::Quest, 60);          // out of range: ignored
        counters.Add(0, Kind::Count, 60);               // no kind: ignored
        Snapshot const s = counters.Take();
        Require(s.samples[2] == 2 && s.seconds[2][std::size_t(Kind::Quest)] == 60 && s.seconds[2][std::size_t(Kind::Idle)] == 60,
            "band 10-14 counted");
        Require(FormatMinuteLine(s) == "[BotActivityMin] L10-14=2/60/0/0/0/60/0 L60+=1/0/0/0/0/0/30", "minute line");
        Require(FormatMinuteLine(counters.Take()) == "[BotActivityMin]", "Take resets");
    }

    // Many threads add, the totals match (relaxed atomics, no lock).
    {
        Counters counters;
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&counters] { for (int i = 0; i < 10000; ++i) counters.Add(3, Kind::Grind, 1); });
        for (std::thread& t : threads)
            t.join();
        Snapshot const s = counters.Take();
        Require(s.samples[3] == 80000 && s.seconds[3][std::size_t(Kind::Grind)] == 80000, "concurrent adds");
    }

    std::cout << "bot_activity_policy_tests passed\n";
    return 0;
}
