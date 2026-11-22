#include "GrindCapPolicy.h"

#include <cstdlib>
#include <iostream>
#include <atomic>
#include <chrono>
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
    using namespace ai::grind_cap;

    // Live: level 3 goblin attacked level 6 Mudpaw Miners (+3) 123 times.
    Require(MaxLevelsAbove(true, 3, 10, 2) == 2, "low-level roster bot: +2 at most");
    Require(MaxLevelsAbove(true, 10, 10, 2) == 4, "from level 10 the old +4 applies");
    Require(MaxLevelsAbove(false, 3, 10, 2) == 4, "other bots unchanged");
    Require(MaxLevelsAbove(true, 3, 0, 2) == 4, "0 disables the rule");
    Require(MaxLevelsAbove(true, 3, 10, 6) == 4, "never looser than before");

    Record record;
    Require(!RecordDeath(record, 1000, 3, 3600, 3600), "first death counts");
    Require(!RecordDeath(record, 1100, 3, 3600, 3600), "second death counts");
    Require(RecordDeath(record, 1200, 3, 3600, 3600), "third death within the hour: avoid");
    Require(IsAvoided(record, 1201) && !IsAvoided(record, 1200 + 3600), "avoided for an hour");

    Record slow;
    Require(!RecordDeath(slow, 1000, 3, 3600, 3600), "one");
    Require(!RecordDeath(slow, 2000, 3, 3600, 3600), "two");
    Require(!RecordDeath(slow, 5000, 3, 3600, 3600), "window over: counting restarts");
    Require(!IsAvoided(slow, 5001), "deaths spread over hours do not avoid");

    // Bounded shared store: reads never create records.
    AvoidStore store;
    Require(!store.IsAvoided(7, 42, 1000) && store.Size() == 0, "a read creates nothing");
    Record state;
    Require(!store.RecordDeath(7, 42, 1000, 3, 3600, 3600, state), "store: first death");
    Require(!store.RecordDeath(7, 42, 1100, 3, 3600, 3600, state), "store: second death");
    Require(store.RecordDeath(7, 42, 1200, 3, 3600, 3600, state) && state.avoidUntil == 4800, "store: third death avoids");
    Require(store.IsAvoided(7, 42, 1300) && !store.IsAvoided(8, 42, 1300) && !store.IsAvoided(7, 43, 1300), "per bot and entry");
    Require(store.Size() == 1, "one record");
    for (std::uint32_t bot = 0; bot < AvoidStore::MaxRecords + 10; ++bot)
        store.RecordDeath(bot, 1, 100000, 3, 3600, 3600, state);
    Require(store.Size() <= AvoidStore::MaxRecords, "store stays bounded");

    // #351 review (OB-00): the store sits in the grind hot path of all map
    // threads. 8 readers and 2 writers in parallel: consistent, bounded, and the
    // read path stays fast (reported, not asserted - CI machines vary).
    {
        AvoidStore shared;
        std::atomic<bool> stop{ false };
        std::atomic<std::uint64_t> reads{ 0 };
        std::vector<std::thread> threads;
        for (int w = 0; w < 2; ++w)
            threads.emplace_back([&, w]
            {
                Record s;
                for (std::uint32_t i = 0; i < 20000; ++i)
                    shared.RecordDeath(std::uint32_t(w * 100000 + i % 3000), i % 50, 100000 + i / 100, 3, 3600, 3600, s);
            });
        auto const started = std::chrono::steady_clock::now();
        for (int r = 0; r < 8; ++r)
            threads.emplace_back([&, r]
            {
                std::uint64_t local = 0;
                while (!stop.load())
                {
                    shared.IsAvoided(std::uint32_t(r * 7 + local % 3000), std::uint32_t(local % 50), 100200);
                    ++local;
                }
                reads += local;
            });
        threads[0].join();
        threads[1].join();
        stop = true;
        for (std::size_t t = 2; t < threads.size(); ++t)
            threads[t].join();
        double const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        Require(shared.Size() > 0 && shared.Size() <= AvoidStore::MaxRecords, "concurrent use stays consistent and bounded");
        std::cout << "AvoidStore stress: " << reads.load() << " reads in " << seconds << " s with 2 writers ("
                  << (seconds > 0 ? double(reads.load()) / seconds / 1e6 : 0) << " M reads/s)" << std::endl;
    }

    Record off;
    for (int i = 0; i < 10; ++i)
        Require(!RecordDeath(off, 1000 + i, 0, 3600, 3600), "0 disables avoidance");

    // Lever 2 (CLI-ROT 22.11.2026): modes 0 off, 1 all, 2 B (odd), 3 A (even), 4 shadow; out of range and threshold 0 = off.
    for (std::uint32_t mode = 0; mode < 8; ++mode)
        for (std::uint32_t guid : { 10u, 11u })
        {
            bool const inB = (guid & 1u) != 0;
            bool const wantApplies = mode == 1 || (mode == 2 && inB) || (mode == 3 && !inB);
            Require(LeverApplies(mode, guid) == wantApplies, "lever applies: 1 all, 2 B, 3 A, never 0 / 4 / out of range");
            Require(LeverComputes(mode, 1) == (mode >= 1 && mode <= 4), "lever computes in modes 1-4 only");
            Require(!LeverComputes(mode, 0), "lever threshold 0 = off");
            if (LeverApplies(mode, guid))
                Require(LeverComputes(mode, 1), "an applied bot always computes");
        }
    Require(JudgeLever(false, true, true) == LeverVerdict::Skip, "applied: lever-avoided candidate skipped");
    Require(JudgeLever(false, true, false) == LeverVerdict::WouldSkip, "shadow: lever-avoided candidate only counted");
    Require(JudgeLever(true, true, true) == LeverVerdict::Keep, "a base-avoided candidate is the base rule's, not the lever's");
    Require(JudgeLever(false, false, true) == LeverVerdict::Keep, "a candidate the lever does not avoid is kept");

    // Threshold 1 in the lever store vs 3 in the base store: one death avoids for the lever only, the base store is untouched.
    AvoidStore base, lever;
    Record r1, r2;
    Require(lever.RecordDeath(5, 77, 1000, 1, 3600, 3600, r1), "lever threshold 1: the first death starts the avoid period");
    Require(lever.IsAvoided(5, 77, 1001), "lever avoids after one death");
    Require(!base.RecordDeath(5, 77, 1000, 3, 3600, 3600, r2), "base threshold 3: one death does not avoid");
    Require(!base.IsAvoided(5, 77, 1001), "base store independent of the lever store");
    Require(!lever.IsAvoided(5, 77, 1000 + 3601), "lever avoid ends after AvoidSeconds");
    Require(&LeverAvoids() != &Avoids(), "lever store is its own store");

    // Counters only increment until taken.
    LeverCounters& c = leverCounters[1];
    c.wouldEmpty.fetch_add(2);
    c.wouldEmpty.fetch_add(1);
    Require(c.wouldEmpty.exchange(0) == 3, "counters are increments, taken once");
    Require(c.wouldEmpty.load() == 0, "taken counters restart at 0");
    return 0;
}
