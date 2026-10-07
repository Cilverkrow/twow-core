#include "ParkPolicy.h"

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
    using namespace ai::park;

    // Nearest inn on the bot's map; another map only when none is on it; -1 without candidates.
    Point const bot{ 0, 100.0f, 100.0f, 0.0f };
    std::vector<Point> inns = { { 1, 101.0f, 101.0f, 0.0f }, { 0, 500.0f, 500.0f, 0.0f }, { 0, 150.0f, 120.0f, 0.0f } };
    Require(NearestInn(bot, inns) == 2, "nearest inn on the same map");
    std::vector<Point> otherMap = { { 1, 0.0f, 0.0f, 0.0f } };
    Require(NearestInn(bot, otherMap) == 0, "other map as fallback");
    Require(NearestInn(bot, {}) == -1, "no inn");

    // Arrival and bind check.
    Require(Arrived({ 0, 120.0f, 100.0f, 0.0f }, { 0, 100.0f, 100.0f, 0.0f }), "arrived within 40 yd");
    Require(!Arrived({ 0, 150.0f, 100.0f, 0.0f }, { 0, 100.0f, 100.0f, 0.0f }), "not arrived beyond 40 yd");
    Require(!Arrived({ 1, 100.0f, 100.0f, 0.0f }, { 0, 100.0f, 100.0f, 0.0f }), "other map is not arrived");
    Require(BindMatches({ 0, 100.5f, 99.5f, 10.2f }, { 0, 100.0f, 100.0f, 10.0f }), "bind within tolerance");
    Require(!BindMatches({ 0, 103.0f, 100.0f, 10.0f }, { 0, 100.0f, 100.0f, 10.0f }), "bind off by 3 yd");
    Require(!BindMatches({ 1, 100.0f, 100.0f, 10.0f }, { 0, 100.0f, 100.0f, 10.0f }), "bind on another map");

    // Retries and the reduced AI tick.
    Require(RetryBind(0) && RetryBind(2) && !RetryBind(3), "three bind tries");
    Require(AiUpdateDue(false, 1000, 0), "first update always");
    Require(!AiUpdateDue(false, 5000, 1000), "out of combat: not before 10 s");
    Require(AiUpdateDue(false, 11000, 1000), "out of combat: after 10 s");
    Require(AiUpdateDue(true, 1001, 1000), "in combat: every tick");
    Require(AiUpdateDue(false, 5000, 4294960000u), "clock wrap");

    // Capacity: the nearest spot with room; overflow to the next one; another map last; all full = -1.
    std::vector<Point> spots = { { 0, 110.0f, 100.0f, 0.0f }, { 0, 300.0f, 100.0f, 0.0f }, { 1, 0.0f, 0.0f, 0.0f } };
    Require(ChooseSpot(bot, spots, { 0, 0, 0 }) == 0, "nearest spot with room");
    Require(ChooseSpot(bot, spots, { SpotCapacity, 3, 0 }) == 1, "full inn overflows to the next spot");
    Require(ChooseSpot(bot, spots, { SpotCapacity, SpotCapacity, 0 }) == 2, "another map when this one is full");
    Require(ChooseSpot(bot, spots, { SpotCapacity, SpotCapacity, SpotCapacity }) == -1, "everything full");
    Require(ChooseSpot(bot, spots, { 24, 0, 0 }) == 0, "24 of 25 still has room");

    // Spacing: every pair of the 25 slots at one spot keeps >= 2 yd.
    Point const spot{ 0, 0.0f, 0.0f, 5.0f };
    for (std::uint32_t a = 0; a < SpotCapacity; ++a)
    {
        Point const pa = SlotOffset(spot, a);
        Require(pa.map == spot.map && pa.z == spot.z, "slot on the spot's map and height");
        Require(Distance2d(pa, spot) >= 2.0f, "slot >= 2 yd from the spot itself");
        for (std::uint32_t b = a + 1; b < SpotCapacity; ++b)
            Require(Distance2d(pa, SlotOffset(spot, b)) >= 2.0f, "slots >= 2 yd apart");
    }

    std::cout << "park_policy_tests passed\n";
    return 0;
}
