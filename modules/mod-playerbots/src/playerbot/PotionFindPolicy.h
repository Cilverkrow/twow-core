#pragma once

#include <cstddef>
#include <cstdint>

// twow-repo#333 T24 POTFIX (OB-00 16.11.2026, bug fix): on this vanilla world data every consumable (item class 0) has subclass 0.
// FindPotionVisitor ("healing potion" / "mana potion") only accepts subclass 1 (potion) and 3 (flask), so it accepted nothing and
// InventoryParseItems fell back to a name match: items whose name contains "healing potion" / "mana potion" - 8 heal and 7 mana
// potions, but also recipes and Craftsman's Writs (class 9 / 15, sorted first), without CanUseItem, and none of the potions with
// another name (rejuvenation potions, draughts, Whipper Root Tuber, Night Dragon's Breath, ...). With AiPlayerbot.PotionFind.BySpell
// on, a subclass-0 consumable is a potion when one of its item spells shares the potion cooldown and has the wanted effect.
// Pure rules, std only (no game types, no config reads, no log lines); the visitor fills the facts from the core spell data. The
// constants mirror the core enums and are pinned against them by the contract potion_find_by_spell_source_contract.
namespace ai::potion_find
{
// Item class and subclass (ItemPrototype.h).
constexpr std::uint32_t kClassConsumable = 0;
constexpr std::uint32_t kSubClassConsumable = 0;

// Spell effects (SpellDefines.h) and the mana power type (SharedDefines.h): the ENERGIZE misc value is the power type.
constexpr std::uint32_t kEffectHeal = 10;
constexpr std::uint32_t kEffectEnergize = 30;
constexpr std::int32_t kPowerMana = 0;

// The potion cooldown category, as UseConsumableAction::IsPotionLike tests it (item slot SpellCategory or the spell Category).
constexpr std::uint32_t kCategoryPotion = 4;

constexpr std::size_t kMaxEffects = 3;

// One item spell slot. known = false: no spell in the slot, or a spell the core does not know.
struct SlotFacts
{
    bool known = false;
    std::uint32_t itemCategory = 0;  // the item slot's SpellCategory
    std::uint32_t spellCategory = 0; // the spell Category
    std::uint32_t effect[kMaxEffects] = {};
    std::int32_t misc[kMaxEffects] = {}; // EffectMiscValue
    bool battlegroundOnly = false;       // the spell casts only in a battleground (SPELL_ATTR_EX3_ONLY_BATTLEGROUNDS)
};

// The slot or its spell is in the potion cooldown category (excludes healthstones 30, mana gems 30, Thistle Tea 60, the teas 1172,
// food 11, drink 59, bandages 150 and elixirs / flasks 79).
inline bool InPotionCategory(SlotFacts const& slot)
{
    return slot.itemCategory == kCategoryPotion || slot.spellCategory == kCategoryPotion;
}

// The spell has the wanted effect; an ENERGIZE effect counts only for mana (excludes the rage potions, power 1).
inline bool HasWantedEffect(SlotFacts const& slot, std::uint32_t effectId)
{
    for (std::size_t i = 0; i < kMaxEffects; ++i)
    {
        if (slot.effect[i] == effectId && (effectId != kEffectEnergize || slot.misc[i] == kPowerMana))
            return true;
    }
    return false;
}

// The new branch of FindPotionVisitor: a consumable of subclass 0 is a potion for effectId when one of its known item spells is in
// the potion cooldown category and has that effect. A slot without a known spell is skipped (the old subclass 1/3 loop stops at
// the first such slot; on these data no class-0 item has its effect behind such a slot). A battleground-only spell (the draughts
// 17348/17349/17351/17352) counts only in a battleground: elsewhere the cast fails with SPELL_FAILED_ONLY_BATTLEGROUNDS.
inline bool AcceptsVanillaPotion(std::uint32_t itemClass, std::uint32_t subClass, SlotFacts const* slots, std::size_t n,
    std::uint32_t effectId, bool inBattleground)
{
    if (itemClass != kClassConsumable || subClass != kSubClassConsumable)
        return false;

    for (std::size_t j = 0; j < n; ++j)
    {
        if (!slots[j].known || (slots[j].battlegroundOnly && !inBattleground))
            continue;

        if (InPotionCategory(slots[j]) && HasWantedEffect(slots[j], effectId))
            return true;
    }
    return false;
}
}
