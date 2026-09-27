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
    return 0;
}
