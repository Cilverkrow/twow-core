#pragma once

#include <cstddef>
#include <cstdint>

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

// twow-repo#601 cr2-C GO diagnosis (CLI-Deep 26.10.2026): a quest item that several quests in the log require was turned down as
// soon as ONE of them had enough - Blood Shard 5075 (Spirit of the Wind 889 needs 10, Blood Shards of Agamaggan 5052 needs 1): with
// one shard and 5052 in the log no further shard was looted (38x loot_not_allowed on a needed item), 889 could never be done. Every
// quest that requires the item adds its need; the item counts as satisfied only when at least one requires it and none needs more.
struct QuestItemNeed
{
    bool required = false;
    bool needsMore = false;
};

inline void AddQuestNeed(QuestItemNeed& need, std::uint32_t have, std::uint32_t required)
{
    need.required = true;
    if (have < required)
        need.needsMore = true;
}

inline bool QuestItemSatisfied(QuestItemNeed const& need)
{
    return need.required && !need.needsMore;
}
}
