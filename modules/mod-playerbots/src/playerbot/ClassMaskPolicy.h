#pragma once

#include <cstdint>

namespace ai::class_mask
{
// twow-repo#742 (AUDIT-L03-6, R2 bug fix, no switch): item_template.AllowableClass is a class MASK (bit = 1 << (class - 1)),
// as Player::CanUseItem reads it (Player.cpp:12325). ItemUsageValue (class consumables, e.g. rogue poisons) and RandomItemMgr
// (stat-less class trinkets) compared it with the class ID (`AllowableClass == class`), which only matched warrior (1) and
// paladin (2): 45 of 53 single-class consumables (all 35 rogue poisons) and 33 class trinkets never matched (tw_world pin 3b).
// "Class-restricted" means the mask leaves out at least one playable class: -1 (0xFFFFFFFF), 32767 and 2047 cover every
// playable class and are NOT restricted (else every consumable would be kept); 0 lets nobody use the item (core) and is not
// "for this class" either. Pure rule, no game types; allPlayableMask = CLASSMASK_ALL_PLAYABLE.
inline bool IsRestrictedTo(std::uint32_t allowableClass, std::uint32_t classMask, std::uint32_t allPlayableMask)
{
    if ((allowableClass & allPlayableMask) == allPlayableMask)
        return false;

    return (allowableClass & classMask) != 0;
}
}
