#include "FormationGridPolicy.h"

#include <cmath>
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

float Far(std::vector<ai::formation_grid::Offset> const& slots)
{
    float far = 0.0f;
    for (auto const& o : slots)
        far = std::max(far, ai::formation_grid::Distance(o));
    return far;
}

float Nearest(std::vector<ai::formation_grid::Offset> const& slots)
{
    float nearest = 1e9f;
    for (size_t a = 0; a < slots.size(); ++a)
        for (size_t b = a + 1; b < slots.size(); ++b)
        {
            float const df = slots[a].forward - slots[b].forward;
            float const ds = slots[a].side - slots[b].side;
            nearest = std::min(nearest, std::sqrt(df * df + ds * ds));
        }
    return nearest;
}
}

int main()
{
    using namespace ai::formation_grid;
    float const spacing = 2.0f;      // owner 2026-09-27
    float const circleMax = 15.0f;   // around the leader
    float const otherMax = 30.0f;    // total extent of the other shapes

    for (Shape shape : { Shape::CIRCLE, Shape::REARGUARD, Shape::VANGUARD, Shape::WEDGE, Shape::TRIANGLE, Shape::BLOCK, Shape::COLUMN })
    {
        float const maxRadius = MaxExtentFor(shape, circleMax, otherMax);
        for (unsigned int n = 1; n <= 40; ++n)
        {
            auto const slots = Slots(shape, n, spacing, maxRadius);
            Require(slots.size() == n, "one slot per follower");
            Require(Far(slots) <= maxRadius + 0.01f, "no slot beyond the maximum radius");
            if (n > 1)
                Require(Nearest(slots) >= 1.0f, "bots may stand close, but never on top of each other");
            for (auto const& o : slots)
                Require(Distance(o) >= 1.0f, "nobody stands on the leader");

            if (shape == Shape::VANGUARD)
                for (auto const& o : slots)
                    Require(o.forward >= -0.01f, "the vanguard stays beside or in front of the leader");
            else if (shape != Shape::CIRCLE)
                for (auto const& o : slots)
                    Require(o.forward <= 0.01f, "every other shape stays beside or behind the leader");
        }
    }

    // Circle: the leader is in the middle.
    auto const circle = Slots(Shape::CIRCLE, 40, spacing, circleMax);
    bool front = false, back = false, left = false, right = false;
    for (auto const& o : circle)
    {
        front |= o.forward > 1.0f;
        back |= o.forward < -1.0f;
        left |= o.side > 1.0f;
        right |= o.side < -1.0f;
    }
    Require(front && back && left && right, "the full circle surrounds the leader");
    Require(Far(circle) <= 8.01f, "40 bots fit in an 8 yd circle (rings of 6/12/18/...)");
    Require(Distance(circle[0]) <= spacing + 0.01f, "the first slots are on the inner ring");

    // Wedge: 40 bots ~20 yd deep instead of ~70 yd (outline at 5 yd).
    auto const wedge = Slots(Shape::WEDGE, 40, spacing, otherMax);
    float deepest = 0.0f;
    for (auto const& o : wedge)
        deepest = std::max(deepest, -o.forward);
    Require(deepest <= 16.01f, "the filled wedge of 40 is 8 rows = 16 yd deep, not ~70");
    Require(std::fabs(wedge[0].side) < 0.01f || std::fabs(wedge[0].side) <= spacing, "the first wedge slot is near the tip");

    // Half ring: at least two rows behind the leader for 20+ bots.
    auto const half = Slots(Shape::REARGUARD, 20, spacing, otherMax);
    float inner = 1e9f, outer = 0.0f;
    for (auto const& o : half)
    {
        inner = std::min(inner, Distance(o));
        outer = std::max(outer, Distance(o));
    }
    Require(outer - inner >= spacing - 0.01f, "the half ring uses at least two rows");

    // Column: two abreast up to 20, three from 21, four from 30.
    Require(ColumnWidth(20) == 2 && ColumnWidth(21) == 3 && ColumnWidth(29) == 3 && ColumnWidth(30) == 4, "column widens for big raids");
    auto const column = Slots(Shape::COLUMN, 10, spacing, otherMax);
    Require(std::fabs(column[0].side) <= spacing && Far(column) <= 5 * spacing + spacing, "10 bots: a short two-abreast column");

    // Block: 40 bots in 7 columns.
    Require(BlockWidth(40) == 7 && BlockWidth(4) == 3 && BlockWidth(100) == 8, "block width follows the square root, 3..8");

    // A 40-bot column needs no compression: 4 abreast, 10 rows = 20 yd.
    auto const column40 = Slots(Shape::COLUMN, 40, spacing, otherMax);
    Require(Far(column40) <= 21.0f && Nearest(column40) >= spacing - 0.01f, "40 bots: 4 abreast, full spacing");
    // The cap compresses when needed; 0 disables it.
    auto const tight = Slots(Shape::COLUMN, 40, spacing, 10.0f);
    Require(Far(tight) <= 10.01f, "a tighter cap compresses the shape");
    auto const uncapped = Slots(Shape::COLUMN, 40, spacing, 0.0f);
    Require(Far(uncapped) > 10.0f, "0 disables the cap");
    Require(MaxExtentFor(Shape::CIRCLE, 15.0f, 30.0f) == 15.0f && MaxExtentFor(Shape::BLOCK, 15.0f, 30.0f) == 30.0f, "circle 15 yd, others 30 yd");
    return 0;
}
