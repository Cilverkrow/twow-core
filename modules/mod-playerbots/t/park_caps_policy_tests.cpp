// twow-repo#551 (owner 10.10.2026): park caps per inn and per city - overflow to the next inn, a full city
// sends bots to an inn outside it, no open-world fallback with a cap on, thread-safe city counts.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "ParkPolicy.h"

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

using namespace ai::park;

// Mirrors RandomPlayerbotMgr::ParkBot's pick: occupancy per candidate through EffectiveOccupied.
int Pick(Point const& bot, std::vector<Point> const& spots, std::vector<std::uint32_t> const& taken,
    std::vector<std::uint32_t> const& zones, CityCounts const& cities, bool inns, std::uint32_t maxPerInn, std::uint32_t maxPerCity)
{
    std::uint32_t const capacity = SpotCapacityFor(inns, maxPerInn);
    std::vector<std::uint32_t> occupied;
    for (std::size_t i = 0; i < spots.size(); ++i)
        occupied.push_back(EffectiveOccupied(taken[i], capacity, zones[i], cities.Get(zones[i]), maxPerCity));
    return ChooseSpot(bot, spots, occupied, capacity);
}
}

int main()
{
    // Off: 25 per spot as before, no city limit, open-world fallback.
    Require(SpotCapacityFor(true, 0) == SpotCapacity && SpotCapacityFor(false, 0) == SpotCapacity, "off: 25 per spot");
    Require(SpotCapacityFor(true, 20) == 20, "MaxPerInn sets the inn capacity");
    Require(SpotCapacityFor(false, 20) == SpotCapacity, "city spots keep 25");
    Require(EffectiveOccupied(7, 25, 1637, 500, 0) == 7, "off: no city limit");
    Require(FallbackHere(0, 0), "off: park where it stands when everything is full");
    Require(!FallbackHere(20, 0) && !FallbackHere(0, 150) && !FallbackHere(20, 150), "a cap on: no open-world park");

    Point const bot{ 1, 0.0f, 0.0f, 0.0f };
    // Inn A in the capital zone 1637 (Orgrimmar), inn B outside (zone 0), inn C further out, on the same map.
    std::vector<Point> const inns = { { 1, 10.0f, 0.0f, 0.0f }, { 1, 50.0f, 0.0f, 0.0f }, { 1, 90.0f, 0.0f, 0.0f } };
    std::vector<std::uint32_t> const zones = { 1637, 0, 0 };
    CityCounts cities;

    std::vector<std::uint32_t> taken = { 0, 0, 0 };
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 150) == 0, "nearest inn with room");
    taken[0] = 19;
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 150) == 0, "19 of 20: still room");
    taken[0] = 20;
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 150) == 1, "inn full at 20: overflow to the next inn");
    Require(Pick(bot, inns, taken, zones, cities, true, 0, 150) == 0, "MaxPerInn 0: 25 as before, 20 is not full");

    taken = { 3, 0, 0 };
    for (int i = 0; i < 150; ++i)
        cities.Add(1637);
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 150) == 1, "city at 150: the inn inside it counts as full");
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 0) == 0, "MaxPerCity 0: no city limit");
    cities.Remove(1637);
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 150) == 0, "149: the city has room again");

    taken = { 20, 20, 20 };
    Require(Pick(bot, inns, taken, zones, cities, true, 20, 150) < 0, "all inns full: no inn");
    std::vector<Point> const citySpots = { { 1, 12.0f, 0.0f, 0.0f } };
    cities.Add(1637);   // back to 150
    Require(Pick(bot, citySpots, { 0 }, { 1637 }, cities, false, 20, 150) < 0, "city full: no city spot either");

    // Counts: zone 0 is never counted, Remove never goes below zero.
    CityCounts counts;
    counts.Add(0);
    Require(counts.Get(0) == 0 && counts.Snapshot().empty(), "zone 0 = not a city");
    counts.Remove(1519);
    Require(counts.Get(1519) == 0, "remove of an empty zone is a no-op");
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&counts, t]()
        {
            for (int i = 0; i < 10000; ++i)
            {
                counts.Add(1519 + std::uint32_t(t % 2));
                counts.Remove(1519 + std::uint32_t(t % 2));
                counts.Add(1637);
            }
        });
    for (std::thread& t : threads)
        t.join();
    Require(counts.Get(1637) == 80000 && counts.Get(1519) == 0 && counts.Get(1520) == 0, "counts exact under threads");

    std::cout << "park_caps_policy_tests passed\n";
    return 0;
}
