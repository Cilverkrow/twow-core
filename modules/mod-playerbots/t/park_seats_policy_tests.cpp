// twow-repo#551 (AiPlayerbot.Park.Seats): chair seats near the innkeeper, bench slots, fallback rings away from the NPC,
// tele targets snapped to the nearest inn.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

float Dist2d(ai::park::Point const& a, ai::park::Point const& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}
}

int main()
{
    using namespace ai::park;

    // Lion's Pride Inn, Goldshire: Innkeeper Farley and two of its chairs (tw_world), plus a street point.
    Point const farley{ 0, -9466.37f, 21.42f, 56.34f };
    Require(SeatNearSpot(farley, Point{ 0, -9458.80f, 13.31f, 56.97f }), "ground floor chair");
    Require(SeatNearSpot(farley, Point{ 0, -9467.19f, 16.16f, 63.93f }), "upper floor chair");
    Require(!SeatNearSpot(farley, Point{ 0, -9449.0f, 68.0f, 57.0f }), "negative: street (tele point) is no seat");
    Require(!SeatNearSpot(farley, Point{ 1, -9458.80f, 13.31f, 56.97f }), "negative: other map");
    Require(!SeatNearSpot(farley, Point{ 0, -9466.0f, 21.0f, 80.0f }), "negative: 24 yd above");

    // A plain chair is one seat, a bench with 3 slots three seats on a line, size apart, centred on the object.
    Require(ChairSeats(farley, 0.0f, 1.0f, 0, 1).size() == 1, "chair without slots = one seat");
    std::vector<Seat> const bench = ChairSeats(Point{ 0, 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f, 3, 2);
    Require(bench.size() == 3, "bench: three seats");
    Require(std::fabs(bench[0].at.y + 1.0f) < 0.01f && std::fabs(bench[1].at.y) < 0.01f && std::fabs(bench[2].at.y - 1.0f) < 0.01f,
        "seats across the bench, 1 yd apart");
    Require(bench[1].height == 2 && bench[1].orientation == 0.0f, "height and facing of the chair");

    // Without a free seat: rings from 5.5 yd on - never the first ring next to the innkeeper.
    for (std::uint32_t slot = 0; slot < 40; ++slot)
        Require(Dist2d(FallbackPlace(farley, slot, 14), farley) >= 5.0f, "fallback keeps >= 4 yd to the innkeeper");
    Require(Dist2d(SlotOffset(farley, 0), farley) < 4.0f, "negative: the old first ring is closer than 4 yd");

    // Tele targets: the Goldshire street point belongs to the Lion's Pride, a far one to no inn.
    std::vector<Point> const inns = { Point{ 0, -8000.0f, 500.0f, 0.0f }, farley };
    Require(SnapToInn(Point{ 0, -9449.0f, 68.0f, 57.0f }, inns) == 1, "Goldshire street -> Lion's Pride");
    Require(SnapToInn(Point{ 0, -9000.0f, -2000.0f, 0.0f }, inns) == -1, "negative: no inn within 60 yd");
    Require(SnapToInn(Point{ 1, -9449.0f, 68.0f, 57.0f }, inns) == -1, "negative: other map");

    std::cout << "park_seats_policy_tests passed\n";
    return 0;
}
