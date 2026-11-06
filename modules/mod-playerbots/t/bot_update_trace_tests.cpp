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

    // twow-repo#601 S1 (OB-10 18.10.): PreValidateTurnIns recorder - time distribution plus open / validated sums.
    CountedRecorder counted;
    for (int i = 0; i < 99; ++i)
        counted.Add(300, 3, 1);               // bucket 8: [256, 512)
    counted.Add(9000, 12, 0);                 // bucket 13: [8192, 16384)
    CountedSnapshot const pv = counted.Take();
    Require(pv.time.calls == 100, "prevalidate calls counted");
    Require(pv.time.totalUs == 99ull * 300 + 9000, "prevalidate sum kept");
    Require(pv.time.maxUs == 9000, "prevalidate max kept");
    Require(PercentileUs(pv.time, 0.50) == 511 && PercentileUs(pv.time, 0.99) == 511, "p50/p99 in the 300 us bucket");
    Require(PercentileUs(pv.time, 1.0) == 16383, "p100 in the 9 ms bucket");
    Require(pv.openSum == 99ull * 3 + 12 && pv.okSum == 99, "open / validated sums");
    Require(pv.time.slowest.empty(), "no slow list");
    CountedSnapshot const pvEmpty = counted.Take();
    Require(pvEmpty.time.calls == 0 && pvEmpty.time.totalUs == 0 && pvEmpty.time.maxUs == 0 && pvEmpty.openSum == 0 &&
        pvEmpty.okSum == 0, "prevalidate Take resets");
    std::uint64_t pvBuckets = 0;
    for (std::uint64_t b : pvEmpty.time.buckets)
        pvBuckets += b;
    Require(pvBuckets == 0, "prevalidate buckets reset");

    std::vector<std::thread> pvThreads;
    for (int t = 0; t < 8; ++t)
        pvThreads.emplace_back([&counted, t]()
        {
            for (int i = 0; i < 10000; ++i)
                counted.Add(uint64_t(i % 500 + t), 2, 1);
        });
    for (std::thread& th : pvThreads)
        th.join();
    CountedSnapshot const pvMany = counted.Take();
    Require(pvMany.time.calls == 80000 && pvMany.openSum == 160000 && pvMany.okSum == 80000, "prevalidate across threads");
    Require(pvMany.time.maxUs == 499 + 7, "prevalidate max across threads");
    Require(&Prevalidate() == &Prevalidate(), "one prevalidate instance");
    Require(static_cast<void*>(&Prevalidate()) != static_cast<void*>(&Global()), "own instance, not the update recorder");

    // A3 step 0 (e): per-cohort volume counter. A = even low guid, B = odd.
    Require(CohortRecorder::IndexOf(2) == 0 && CohortRecorder::IndexOf(3) == 1, "guid 2 -> A, 3 -> B");
    Require(CohortRecorder::IndexOf(0) == 0 && CohortRecorder::IndexOf(4294967295u) == 1, "parity at the guid edges");
    CohortRecorder cohortRec;
    cohortRec.Add(2, 9999);       // A: below 10 ms
    cohortRec.Add(4, 10000);      // A: exactly 10 ms
    cohortRec.Add(6, 19999);      // A: only over10
    cohortRec.Add(8, 20000);      // A: both
    cohortRec.Add(3, 5);          // B
    CohortSnapshot const cs = cohortRec.Take();
    Require(cs.cohort[0].calls == 4 && cs.cohort[1].calls == 1, "calls per cohort");
    Require(cs.over10[0] == 3 && cs.over20[0] == 1, "thresholds exact: 9999 -> none, 10000 / 19999 -> over10, 20000 -> both");
    Require(cs.over10[1] == 0 && cs.over20[1] == 0, "cohort B has no slow call");
    Require(cs.cohort[0].totalUs == 9999ull + 10000 + 19999 + 20000 && cs.cohort[0].maxUs == 20000, "A sum and max");
    Require(cs.cohort[1].totalUs == 5 && cs.cohort[1].maxUs == 5, "B sum and max");
    for (std::size_t c = 0; c < CohortCount; ++c)
    {
        std::uint64_t sum = 0;
        for (std::uint64_t b : cs.cohort[c].buckets)
            sum += b;
        Require(sum == cs.cohort[c].calls, "calls == sum of buckets");
    }
    Require(PercentileUs(cs.cohort[0], 0.99) == BucketUpperUs(BucketOf(20000)), "A p99 in the 20 ms bucket");
    CohortSnapshot const cohortEmpty = cohortRec.Take();
    for (std::size_t c = 0; c < CohortCount; ++c)
    {
        std::uint64_t sum = 0;
        for (std::uint64_t b : cohortEmpty.cohort[c].buckets)
            sum += b;
        Require(cohortEmpty.cohort[c].calls == 0 && cohortEmpty.cohort[c].totalUs == 0 && cohortEmpty.cohort[c].maxUs == 0 &&
            cohortEmpty.over10[c] == 0 && cohortEmpty.over20[c] == 0 && sum == 0, "second Take is all zeros");
    }

    std::uint64_t rng = 88172645463325252ull;
    std::uint64_t expectOver10[2] = { 0, 0 };
    std::uint64_t expectCalls[2] = { 0, 0 };
    for (int i = 0; i < 20000; ++i)
    {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        std::uint32_t const guid = std::uint32_t(rng >> 40);
        std::uint64_t const us = (rng >> 8) % 40000;
        cohortRec.Add(guid, us);
        ++expectCalls[guid & 1u];
        if (us >= 10000)
            ++expectOver10[guid & 1u];
    }
    CohortSnapshot const cohortRand = cohortRec.Take();
    for (std::size_t c = 0; c < CohortCount; ++c)
    {
        Require(cohortRand.over20[c] <= cohortRand.over10[c] && cohortRand.over10[c] <= cohortRand.cohort[c].calls, "over20 <= over10 <= calls");
        Require(cohortRand.cohort[c].calls == expectCalls[c] && cohortRand.over10[c] == expectOver10[c], "random run matches the reference count");
    }

    std::vector<std::thread> cohortThreads;
    for (int t = 0; t < 8; ++t)
        cohortThreads.emplace_back([&cohortRec, t]()
        {
            for (int i = 0; i < 10000; ++i)
                cohortRec.Add(std::uint32_t(t), std::uint64_t(i % 500));
        });
    for (std::thread& th : cohortThreads)
        th.join();
    CohortSnapshot const cohortMany = cohortRec.Take();
    Require(cohortMany.cohort[0].calls == 40000 && cohortMany.cohort[1].calls == 40000, "cohort counts across threads");
    Require(&Cohort() == &Cohort(), "one cohort instance");
    Require(static_cast<void*>(&Cohort()) != static_cast<void*>(&Global()), "own instance, not the update recorder");

    std::cout << "bot_update_trace_tests passed\n";
    return 0;
}
