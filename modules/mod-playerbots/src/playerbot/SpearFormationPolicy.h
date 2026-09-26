#pragma once

namespace ai::spear_formation
{
// #290: the 10th formation, "spear" (wedge). The follow target is the tip;
// members take slots alternately left and right behind it, each pair one rank
// further back and further out, so the group forms a V pointing forward.
// Slot 0 is the closest (tanks first), the last slots the rear (healers).

// How far out a rank stands per rank back: 0.7 gives a ~35 degree half angle,
// wide enough that neighbours do not stack on narrow paths.
constexpr float SideRatio = 0.7f;

struct Offset
{
    float forward; // along the leader's facing; negative is behind
    float side;    // to the leader's left (positive) or right (negative)
};

inline unsigned int Rank(unsigned int slot)
{
    return slot / 2 + 1;
}

inline Offset SlotOffset(unsigned int slot, float spacing)
{
    float const rank = static_cast<float>(Rank(slot));
    float const side = (slot % 2 == 0) ? 1.0f : -1.0f;
    return { -spacing * rank, side * spacing * rank * SideRatio };
}
}
