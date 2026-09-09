#pragma once

#include <array>
#include <cstddef>

namespace ai { namespace roster { namespace bags
{
constexpr std::size_t kBagSlotCount = 4;

struct BagSlotState
{
    bool occupied = false;
    bool rangedContainer = false;
};

// This policy only decides which *empty* equipped bag slots may be filled.
// It deliberately has no Player, Item or database dependency so the runtime
// caller cannot accidentally turn a roster-login repair into randomisation.
inline std::array<bool, kBagSlotCount> SelectEmptySlots(bool hunter,
    std::array<BagSlotState, kBagSlotCount> const& slots)
{
    bool hasRangedContainer = false;
    std::size_t occupied = 0;
    for (BagSlotState const& slot : slots)
    {
        if (slot.occupied)
            ++occupied;
        hasRangedContainer = hasRangedContainer || slot.rangedContainer;
    }

    // A hunter without an arrow/bullet container retains one physical bag
    // slot. Existing items are never removed merely to establish that reserve.
    std::size_t const capacity = hunter && !hasRangedContainer ? kBagSlotCount - 1 : kBagSlotCount;
    std::array<bool, kBagSlotCount> provision{};
    for (std::size_t index = 0; index < slots.size() && occupied < capacity; ++index)
    {
        if (!slots[index].occupied)
        {
            provision[index] = true;
            ++occupied;
        }
    }
    return provision;
}
}}} // namespace ai::roster::bags
