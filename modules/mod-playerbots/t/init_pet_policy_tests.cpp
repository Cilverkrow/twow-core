#include "InitPetPolicy.h"

#include <cstdlib>
#include <iostream>
#include <set>

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
    using namespace ai::init_pet;

    // (MinLevel, entry) as InitPet collects them, unsorted.
    TameList list = { {10, 3000}, {1, 1200}, {30, 700}, {10, 2000}, {60, 9000}, {1, 1100} };
    SortTameList(list);
    Require(list.front() == std::make_pair(1u, 1100u) && list.back() == std::make_pair(60u, 9000u), "sorted by level, then entry");

    // The candidates for a level are exactly the entries with MinLevel <= level (same set as the old scan).
    auto candidates = [&](std::uint32_t level)
    {
        std::set<std::uint32_t> ids;
        for (std::size_t i = 0; i < CandidatesFor(list, level); ++i)
            ids.insert(list[i].second);
        return ids;
    };
    auto scan = [&](std::uint32_t level)
    {
        std::set<std::uint32_t> ids;
        for (auto const& [minLevel, id] : list)
            if (minLevel <= level)
                ids.insert(id);
        return ids;
    };
    for (std::uint32_t level : {0u, 1u, 9u, 10u, 11u, 30u, 59u, 60u, 70u})
        Require(candidates(level) == scan(level), "prefix equals the scan for every level");
    Require(CandidatesFor(list, 0) == 0, "level 0: none");
    Require(CandidatesFor(list, 10) == 4, "level 10: two of level 1 and two of level 10");
    Require(CandidatesFor(TameList(), 60) == 0, "empty list");

    // Stored-pet cache: valid within 60 s, never with stamp 0.
    Require(!CacheValid(0, 100, StoredPetCacheSeconds), "never checked");
    Require(CacheValid(100, 100, StoredPetCacheSeconds) && CacheValid(100, 159, StoredPetCacheSeconds), "within a minute");
    Require(!CacheValid(100, 160, StoredPetCacheSeconds), "expired after a minute");
    Require(!CacheValid(200, 100, StoredPetCacheSeconds), "clock went back: ask again");

    // Cooldown after a try without a pet.
    Require(!InCooldown(0, 100), "no cooldown set");
    Require(InCooldown(160, 100) && !InCooldown(160, 160), "until the stamp");

    std::cout << "init_pet_policy_tests passed\n";
    return 0;
}
