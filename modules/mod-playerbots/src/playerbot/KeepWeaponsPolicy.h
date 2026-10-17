#pragma once

// twow-repo#308 EQUIP-FLIP-01 (LOOT-ROLE test 16.10.2026, approved by OB-00): EquipUpgradesAction took off main hand and
// off hand on every run (every looted weapon or armour piece) and equipped them again - 169 bots equipped the same starter
// weapon 3+ times in 10 min. With AiPlayerbot.Equip.KeepWeaponsUnlessUpgrade the weapons only come off when a weapon,
// shield or held item in the bags is an equip candidate; and a re-equip of the old main/off hand is no EquipAction event.

#include <cstdint>

namespace ai::keep_weapons
{
    // Inventory types that go into the main or off hand (classic INVTYPE_* values).
    constexpr std::uint32_t InvWeapon = 13;          // INVTYPE_WEAPON (one hand)
    constexpr std::uint32_t InvShield = 14;          // INVTYPE_SHIELD
    constexpr std::uint32_t InvTwoHand = 17;         // INVTYPE_2HWEAPON
    constexpr std::uint32_t InvMainHand = 21;        // INVTYPE_WEAPONMAINHAND
    constexpr std::uint32_t InvOffHand = 22;         // INVTYPE_WEAPONOFFHAND
    constexpr std::uint32_t InvHoldable = 23;        // INVTYPE_HOLDABLE

    inline bool IsHandItem(std::uint32_t inventoryType)
    {
        return inventoryType == InvWeapon || inventoryType == InvShield || inventoryType == InvTwoHand ||
            inventoryType == InvMainHand || inventoryType == InvOffHand || inventoryType == InvHoldable;
    }

    // Take main and off hand off for this run? Always without the switch (as before); with it only when a hand item
    // in the bags is an equip candidate (EQUIP or BAD_EQUIP).
    inline bool UnequipHands(bool keepSwitch, bool handCandidateInBags)
    {
        return !keepSwitch || handCandidateInBags;
    }
}
