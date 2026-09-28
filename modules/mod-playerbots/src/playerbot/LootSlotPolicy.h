#pragma once

#include <cstddef>

namespace ai::loot_slot
{
// #405 (train 7 live, 2026-09-28): the loot packet lists the normal (shared) items
// first and then the player's own quest-only items. Loot::FillQuestLoot marks each
// quest item is_blocked when it enters a player's quest list - a reservation for
// that player, not a lock against it. Only the shared normal slots need the
// shared-loot right check (blocked by a roll, owned by another looter).

inline bool IsQuestSlot(std::size_t slot, std::size_t normalItems)
{
    return slot >= normalItems;
}

// sharedAllowed = GetSlotTypeForSharedLoot(...) != MAX_LOOT_SLOT_TYPE.
inline bool MayTake(bool questSlot, bool isBlocked, bool sharedAllowed)
{
    if (questSlot)
        return true;  // resolved per player by Loot::LootItemInSlot (looted -> null)
    return !isBlocked && sharedAllowed;
}
}
