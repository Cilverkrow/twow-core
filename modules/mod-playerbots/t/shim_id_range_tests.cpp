#include "ShimIdRange.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <unordered_map>
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

template <class Range>
std::vector<std::uint32_t> Collect(Range const& range)
{
    std::vector<std::uint32_t> ids;
    for (std::uint32_t id : range)
        ids.push_back(id);
    return ids;
}

struct State
{
    std::uint32_t ID;
};
}

int main()
{
    using namespace cmangos_shim;

    // Mode 0 is the old loop: exactly [lo, hi), the same lookups, also for ids that are not loaded.
    std::vector<std::uint32_t> const counted = Collect(IdRange::Count(0, 5));
    Require(counted == std::vector<std::uint32_t>({0, 1, 2, 3, 4}), "counting mode yields [0, cap)");
    std::vector<std::uint32_t> const travelNode = Collect(IdRange::Count(1, 4));
    Require(travelNode == std::vector<std::uint32_t>({1, 2, 3}), "Ids(1, cap + 1) is the old 'entry = 1; entry <= cap' loop");
    Require(Collect(IdRange::Count(7, 7)).empty(), "empty count range");
    Require(Collect(IdRange::Count(9, 3)).empty(), "hi below lo yields nothing (no wrap)");

    std::size_t n = 0;
    for (std::uint32_t id : IdRange::Count(0, 100000))
        n += id < 100000 ? 1 : 0;
    Require(n == 100000, "the full legacy creature cap is counted");

    // Mode 1 walks the sorted ids of the template map, above the old caps too.
    std::unordered_map<std::uint32_t, int> templates = { {2509000, 0}, {6, 0}, {100000, 0}, {99999, 0}, {7000035, 0}, {1, 0} };
    std::vector<std::uint32_t> const keys = SortedKeys(templates);
    Require(keys == std::vector<std::uint32_t>({1, 6, 99999, 100000, 2509000, 7000035}), "SortedKeys is ascending and complete");
    Require(Collect(IdRange::Set(keys, 0)) == keys, "set mode yields every id in ascending order");
    Require(Collect(IdRange::Set(keys, 2)) == std::vector<std::uint32_t>({6, 99999, 100000, 2509000, 7000035}), "set mode starts at lo");
    Require(Collect(IdRange::Set(keys, 8000000)).empty(), "lo above every id yields nothing");
    std::vector<std::uint32_t> const none;
    Require(Collect(IdRange::Set(none, 0)).empty(), "an empty set yields nothing");

    // A2: off counts 0 .. cap, on walks the IDs of the reputation list in list order (ids >= cap too).
    std::map<std::uint32_t, State> stateList = { {0, {72}}, {3, {529}}, {7, {47}}, {12, {270}} };
    Require(Collect(FactionIdRange<std::map<std::uint32_t, State>>(stateList, false, 4)) == std::vector<std::uint32_t>({0, 1, 2, 3}),
        "faction ids off: the old loop to the cap");
    Require(Collect(FactionIdRange<std::map<std::uint32_t, State>>(stateList, true, 4)) == std::vector<std::uint32_t>({72, 529, 47, 270}),
        "faction ids on: every reputation list faction");
    std::map<std::uint32_t, State> const emptyList;
    Require(Collect(FactionIdRange<std::map<std::uint32_t, State>>(emptyList, true, 100)).empty(), "an empty reputation list yields nothing");

    std::cout << "shim_id_range tests passed\n";
    return 0;
}
