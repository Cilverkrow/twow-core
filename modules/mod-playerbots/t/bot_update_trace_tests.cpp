#include "BotUpdateTrace.h"

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
    using namespace ai::bot_update;

    Require(BucketOf(0) == 0 && BucketOf(1) == 0, "0 and 1 us in bucket 0");
    Require(BucketOf(2) == 1 && BucketOf(3) == 1, "2..3 us in bucket 1");
    Require(BucketOf(1024) == 10, "1024 us in bucket 10");
    Require(BucketOf(~0ull) == Buckets - 1, "huge values in the last bucket");
    Require(BucketUpperUs(10) == 2047, "upper edge of bucket 10");

    Recorder recorder;
    int described = 0;
    auto describe = [&described]() { ++described; return std::string("bot"); };
    for (int i = 0; i < 990; ++i)
        recorder.Add(100, describe);          // bucket 6: [64, 128)
    for (int i = 0; i < 9; ++i)
        recorder.Add(5000, describe);         // bucket 12: [4096, 8192)
    recorder.Add(30000, describe);            // bucket 14: [16384, 32768)

    Snapshot const s = recorder.Take();
    Require(s.calls == 1000, "calls counted");
    Require(s.maxUs == 30000, "max kept");
    Require(s.totalUs == 990ull * 100 + 9ull * 5000 + 30000, "total kept");
    Require(PercentileUs(s, 0.50) == 127, "p50 at the 100 us bucket");
    Require(PercentileUs(s, 0.99) == 127, "p99 still in the 100 us bucket (990/1000)");
    Require(PercentileUs(s, 0.999) == 8191, "p999 in the 5 ms bucket");
    Require(PercentileUs(s, 1.0) == 32767, "p100 in the 30 ms bucket");
    Require(CallsAtLeastUs(s, 10000) == 1, "one call over 10 ms (bucket from 16 ms)");
    Require(CallsAtLeastUs(s, 4096) == 10, "ten calls from 4 ms");
    Require(s.slowest.size() == SlowestKept, "five slowest kept");
    Require(s.slowest.front().us == 30000 && s.slowest[1].us == 5000, "slowest first");
    Require(described < 1000, "the slow list does not describe every call");

    Snapshot const empty = recorder.Take();
    Require(empty.calls == 0 && empty.maxUs == 0 && empty.slowest.empty(), "Take resets");
    Require(PercentileUs(empty, 0.5) == 0, "no calls, no percentile");

    // Many map threads at once.
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&recorder, t]()
        {
            for (int i = 0; i < 20000; ++i)
                recorder.Add(uint64_t(i % 1000 + t), []() { return std::string("x"); });
        });
    for (std::thread& th : threads)
        th.join();
    Snapshot const many = recorder.Take();
    Require(many.calls == 8ull * 20000, "every call counted across threads");
    std::uint64_t inBuckets = 0;
    for (std::uint64_t b : many.buckets)
        inBuckets += b;
    Require(inBuckets == many.calls, "buckets add up to the calls");
    Require(many.slowest.size() == SlowestKept && many.slowest.front().us == many.maxUs, "slowest matches max");

    std::cout << "bot_update_trace_tests passed\n";
    return 0;
}
