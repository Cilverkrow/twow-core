#include "SpearFormationPolicy.h"

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

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}
}

int main()
{
    using namespace ai::spear_formation;
    float const spacing = 2.0f;

    Offset const first = SlotOffset(0, spacing);
    Offset const second = SlotOffset(1, spacing);
    Require(first.forward < 0.0f && second.forward < 0.0f, "every slot is behind the tip");
    Require(Near(first.forward, second.forward), "a pair shares one rank");
    Require(first.side > 0.0f && second.side < 0.0f, "a pair takes both legs of the V");
    Require(Near(first.side, -second.side), "the V is symmetric");

    for (unsigned int slot = 0; slot + 2 < 40; ++slot)
    {
        Offset const now = SlotOffset(slot, spacing);
        Offset const next = SlotOffset(slot + 2, spacing);
        Require(next.forward < now.forward, "each rank stands further back");
        Require(std::fabs(next.side) > std::fabs(now.side), "each rank stands further out");
        Require(Near(now.side / now.forward, -SideRatio) || Near(now.side / now.forward, SideRatio),
            "all slots lie on the two straight legs");
    }

    for (unsigned int a = 0; a < 40; ++a)
        for (unsigned int b = a + 1; b < 40; ++b)
        {
            Offset const pa = SlotOffset(a, spacing);
            Offset const pb = SlotOffset(b, spacing);
            float const dx = pa.forward - pb.forward;
            float const dy = pa.side - pb.side;
            Require(std::sqrt(dx * dx + dy * dy) >= spacing * 0.99f, "no two members stand closer than the follow spacing");
        }

    Require(Rank(0) == 1 && Rank(1) == 1 && Rank(2) == 2 && Rank(39) == 20, "two slots per rank");
    return 0;
}
