#include "MemStoresPolicy.h"
#include "Objects/TrackedCount.h"

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

int main()
{
    using namespace ai::mem_stores;

    Require(Due(0, 1000), "the first call reports (baseline)");
    Require(!Due(1000, 4599), "not before an hour");
    Require(Due(1000, 4600), "after an hour");

    std::string const status = "Name:\tmangosd\nVmPeak:\t 7000000 kB\nVmRSS:\t 6532108 kB\nRssAnon:\t 6000000 kB\n";
    Require(ParseVmRssKb(status) == 6532108, "VmRSS read from /proc/self/status");
    Require(ParseVmRssKb("Name: x\n") == 0, "no VmRSS: 0");

    std::vector<MapStat> stats = {
        { 0, 0, 310, 42000, 9000, 90 },
        { 1, 0, 402, 51000, 11000, 88 },
        { 36, 7, 4, 300, 80, 2 },
        { 30, 0, 9, 700, 50, 0 },
    };
    Require(TopMaps(stats, 3) == "1:402/51000/11000,0:310/42000/9000,30:9/700/50", "top maps by grids");
    Require(TopMaps({ { 36, 7, 4, 300, 80, 2 } }, 3) == "36/7:4/300/80", "an instance shows its id");

    // #416 (7.3): value-cache names.
    Require(BaseValueName("item usage::6948") == "item usage", "qualifier cut off");
    Require(BaseValueName("travel target") == "travel target", "plain name kept");
    std::map<std::string, uint32_t> counts = { { "item usage", 900 }, { "spell id", 120 }, { "distance", 450 }, { "item count", 450 } };
    Require(TopCounts(counts, 3) == "item usage:900,distance:450,item count:450", "top value names, ties by name");

    // #452: malloc arenas and live counts through copies and destruction.
    Require(CountArenas("<malloc version=\"1\">\n<heap nr=\"0\">\n</heap>\n<heap nr=\"1\">\n</heap>\n<total type=\"fast\"/>") == 2,
            "two arenas");
    Require(CountArenas("") == 0, "no xml, no arenas");
    struct Tag {};
    {
        TrackedCount<Tag> a{1};
        TrackedCount<Tag> b(a);
        Require(TrackedCount<Tag>::Total() == 2, "copy counted");
        TrackedCount<Tag> points;
        points.Set(40);
        points.Set(25);
        Require(TrackedCount<Tag>::Total() == 27, "set replaces the old value");
        b = a;
        Require(TrackedCount<Tag>::Total() == 27, "assignment keeps one per object");
        points = a;
        Require(TrackedCount<Tag>::Total() == 3, "assigned value replaces the old one");
    }
    Require(TrackedCount<Tag>::Total() == 0, "destruction subtracts");

    std::cout << "mem_stores_policy_tests passed\n";
    return 0;
}
