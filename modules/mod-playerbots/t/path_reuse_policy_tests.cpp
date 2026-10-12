#include "PathReusePolicy.h"

#include <cstdlib>
#include <iostream>

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

// twow-repo#541 (L1): pure policy of AiPlayerbot.Perf.PartialPathReuse. The integration (ResolveMovePath, the stamp
// in LastMovement, the [PathReuse] line) is pinned by t/partial_path_reuse_source_contract_tests.cmake.
int main()
{
    using namespace ai::path_reuse;

    Require(hits.load() == 0 && misses.load() == 0, "counters start at zero");

    // Age limit: default 10 s, clamped to 1..60 s.
    Require(DefaultMs == 10000, "default 10000 ms");
    Require(ClampMs(10000) == 10000, "default passes the clamp");
    Require(ClampMs(0) == MinMs && ClampMs(-5) == MinMs && ClampMs(999) == MinMs, "below 1000 -> 1000");
    Require(ClampMs(1000) == 1000 && ClampMs(60000) == 60000, "edges kept");
    Require(ClampMs(60001) == MaxMs && ClampMs(2147483647) == MaxMs, "above 60000 -> 60000");

    // All conditions hold -> reuse.
    Require(MayReuse(true, true, true, true, false, false, 0, 10000), "fresh stamp, same path and destination");
    Require(MayReuse(true, true, true, true, false, false, 9999, 10000), "just below the age limit");
    // Each condition alone blocks it.
    Require(!MayReuse(false, true, true, true, false, false, 0, 10000), "no stamp (switch was off, clear())");
    Require(!MayReuse(true, false, true, true, false, false, 0, 10000), "lastPath is another path now");
    Require(!MayReuse(true, true, false, true, false, false, 0, 10000), "destination changed");
    Require(!MayReuse(true, true, true, false, false, false, 0, 10000), "bot changed map");
    Require(!MayReuse(true, true, true, true, true, false, 0, 10000), "end of the path reached");
    Require(!MayReuse(true, true, true, true, false, true, 0, 10000), "on a transport");
    Require(!MayReuse(true, true, true, true, false, false, 10000, 10000), "age limit reached");
    Require(!MayReuse(true, true, true, true, false, false, 4000000000u, 10000), "very old stamp");

    std::cout << "path_reuse_policy tests passed\n";
    return 0;
}
