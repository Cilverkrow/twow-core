#include "MemStoresPolicy.h"

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

    std::cout << "mem_stores_policy_tests passed\n";
    return 0;
}
