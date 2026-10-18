#include "GrindPickPolicy.h"

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
    using namespace ai::grind_pick;

    // Bands: L1-5 .. L56-60, above 60 in the last one, 0 in the first.
    Require(BandOf(1) == 0 && BandOf(5) == 0, "L1-5 in band 0");
    Require(BandOf(6) == 1 && BandOf(10) == 1, "L6-10 in band 1");
    Require(BandOf(15) == 2 && BandOf(19) == 3, "L15 band 2, L19 band 3");
    Require(BandOf(60) == Bands - 1 && BandOf(63) == Bands - 1, "L60 and above in the last band");
    Require(BandOf(0) == 0, "level 0 in band 0");

    // Source of a target from "possible targets": the bot as victim first, then the quest need, else grind.
    Require(PickedSource(true, true) == Source::Victim, "victim wins over quest (self-defence)");
    Require(PickedSource(false, true) == Source::Quest, "quest need");
    Require(PickedSource(false, false) == Source::Grind, "plain grind pick");

    Counters counters;
    counters.Add(BandOf(17), Source::Attacker, true);
    counters.Add(BandOf(17), Source::Attacker, false);
    counters.Add(BandOf(17), Source::Quest, true);
    counters.Add(BandOf(17), Source::Grind, false);
    counters.Add(BandOf(17), Source::Victim, true);
    counters.Add(BandOf(17), Source::None, true);   // ignored
    counters.Add(Bands, Source::Grind, true);       // ignored (no such band)
    Snapshot const s = counters.Take();
    std::size_t const b = BandOf(17);
    Require(s.picks[b][std::size_t(Source::Attacker)] == 2 && s.grey[b][std::size_t(Source::Attacker)] == 1, "attacker 2/1");
    Require(s.picks[b][std::size_t(Source::Quest)] == 1 && s.grey[b][std::size_t(Source::Quest)] == 1, "quest 1/1");
    Require(s.picks[b][std::size_t(Source::Grind)] == 1 && s.grey[b][std::size_t(Source::Grind)] == 0, "grind 1/0");
    Require(s.picks[b][std::size_t(Source::Victim)] == 1 && s.grey[b][std::size_t(Source::Victim)] == 1, "victim 1/1");
    Require(s.picks[b][std::size_t(Source::None)] == 0, "None never counted");
    Require(!BandEmpty(s, b) && BandEmpty(s, 0), "band with picks / empty band");
    Require(FormatLine(s, b) == "[GrindPick] band=L16-20 attacker=2/1 victim=1/1 quest=1/1 grind=1/0", "line format");

    Snapshot const empty = counters.Take();
    Require(BandEmpty(empty, b), "Take resets");

    // Many map threads at once.
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&counters, t]()
        {
            for (int i = 0; i < 10000; ++i)
                counters.Add(std::size_t(i % 12), Source(1 + (i + t) % 4), i % 3 == 0);
        });
    for (std::thread& th : threads)
        th.join();
    Snapshot const many = counters.Take();
    std::uint64_t picks = 0, grey = 0;
    for (std::size_t band = 0; band < Bands; ++band)
        for (std::size_t i = 0; i < SourceCount; ++i)
        {
            picks += many.picks[band][i];
            grey += many.grey[band][i];
        }
    Require(picks == 80000, "every pick counted across threads");
    Require(grey == 8ull * 3334, "every grey pick counted across threads");

    std::cout << "grind_pick_policy_tests passed\n";
    return 0;
}
