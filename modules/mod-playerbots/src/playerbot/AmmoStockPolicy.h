#pragma once

#include <cstdint>

namespace ai::ammo_stock
{
// OB-10 train 6: a warrior bought 142 Crude Throwing Axes. Thrown weapons stack
// and are used up per throw like ammo, and the ammo rule asked for 2 full stacks
// (400 axes) for warriors and rogues and 8 stacks for a hunter's arrows. The
// wanted stock is capped by config; 0 leaves the class default.
inline float NeededStacks(bool hunter, bool thrown, std::uint32_t maxAmmoStacks, std::uint32_t maxThrownStacks)
{
    float need = hunter && !thrown ? 8.0f : 2.0f;
    std::uint32_t const cap = thrown ? maxThrownStacks : maxAmmoStacks;
    if (cap && float(cap) < need)
        need = float(cap);
    return need;
}
}
