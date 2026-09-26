// twow-repo#351: nearest-rank percentiles and slow counters of the aggregated
// tick statistics.
#include "../src/game/TickStats.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    int failures = 0;

    void Expect(uint32_t actual, uint32_t expected, char const* label)
    {
        if (actual != expected)
        {
            std::cerr << label << ": got " << actual << ", expected " << expected << "\n";
            ++failures;
        }
    }
}

int main()
{
    {
        std::vector<uint32_t> none;
        TickStatsSummary s = SummarizeTickStats(none);
        Expect(s.count, 0, "empty: count");
        Expect(s.max, 0, "empty: max");
        Expect(s.p99, 0, "empty: p99");
    }
    {
        std::vector<uint32_t> one{ 37 };
        TickStatsSummary s = SummarizeTickStats(one);
        Expect(s.p50, 37, "single: p50");
        Expect(s.p99, 37, "single: p99");
        Expect(s.max, 37, "single: max");
    }
    {
        // 1..300 in reverse: nearest rank gives exactly the percentile value,
        // and both slow counters count strictly above their threshold.
        std::vector<uint32_t> v;
        for (uint32_t i = 300; i >= 1; --i)
            v.push_back(i);
        TickStatsSummary s = SummarizeTickStats(v);
        Expect(s.count, 300, "1..300: count");
        Expect(s.p50, 150, "1..300: p50");
        Expect(s.p95, 285, "1..300: p95");
        Expect(s.p99, 297, "1..300: p99");
        Expect(s.max, 300, "1..300: max");
        Expect(s.over100, 200, "1..300: over 100 ms");
        Expect(s.over200, 100, "1..300: over 200 ms");
    }
    {
        // A realistic minute: 1195 ticks of 50 ms, 4 of 150 ms, one 2346 ms
        // start spike. p99 stays at the normal tick, max shows the spike.
        std::vector<uint32_t> v(1195, 50);
        for (int i = 0; i < 4; ++i)
            v.push_back(150);
        v.push_back(2346);
        TickStatsSummary s = SummarizeTickStats(v);
        Expect(s.count, 1200, "minute: count");
        Expect(s.p50, 50, "minute: p50");
        Expect(s.p99, 50, "minute: p99 below the 1 % tail");
        Expect(s.max, 2346, "minute: max");
        Expect(s.over100, 5, "minute: over 100 ms");
        Expect(s.over200, 1, "minute: over 200 ms");
    }
    {
        // 2 % slow ticks move p99 onto the slow value.
        std::vector<uint32_t> v(98, 40);
        v.push_back(900);
        v.push_back(1200);
        TickStatsSummary s = SummarizeTickStats(v);
        Expect(s.p99, 900, "2 % tail: p99");
        Expect(s.max, 1200, "2 % tail: max");
    }
    {
        // Exactly on a threshold is not slow.
        std::vector<uint32_t> v{ 100, 200 };
        TickStatsSummary s = SummarizeTickStats(v);
        Expect(s.over100, 1, "boundary: 100 is not over 100");
        Expect(s.over200, 0, "boundary: 200 is not over 200");
    }

    if (failures)
    {
        std::cerr << failures << " tick statistics expectation(s) failed\n";
        return 1;
    }
    std::cout << "tick statistics: all expectations hold\n";
    return 0;
}
