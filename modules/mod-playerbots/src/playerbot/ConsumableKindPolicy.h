#pragma once

#include <cstddef>
#include <cstdint>

// twow-repo#333 T24 CLSFIX (OB-00 16.11.2026, bug fix): on this vanilla world data every consumable (item class 0) has subclass 0,
// so the potion / elixir / flask / scroll branches of UseConsumableAction::ClassifyConsumable never ran and "use consumable" only
// knew buff food. With AiPlayerbot.Consumables.KindFromSpell on, the action derives the subclass of a subclass-0 consumable from
// its item spells here and runs the unchanged branches with it. Pure rules, std only (no game types, no config reads, no log
// lines); the action fills the facts from the core spell data. The constants mirror the core enums and are pinned against them
// by the contract consumables_kind_from_spell_source_contract.
namespace ai::consumable_kind
{
// Item subclasses of class 0 (ItemPrototype.h).
constexpr std::uint32_t kSubClassConsumable = 0;
constexpr std::uint32_t kSubClassPotion = 1;
constexpr std::uint32_t kSubClassElixir = 2;
constexpr std::uint32_t kSubClassFlask = 3;
constexpr std::uint32_t kSubClassScroll = 4;

// Spell families (DBCEnums.h), the friendly-unit target (SpellDefines.h) and the apply-aura effect (SpellDefines.h).
constexpr std::uint32_t kFamilyGeneric = 0;
constexpr std::uint32_t kFamilyPotion = 13;
constexpr std::uint32_t kTargetUnitFriend = 21;
constexpr std::uint32_t kEffectApplyAura = 6;

// Auras (SpellAuraDefines.h): the stat buff auras of UseConsumableAction::IsStatBuffSpell, plus 34, 35, 49 and 71 (variant B).
constexpr std::uint32_t kAuraModDamageDone = 13;
constexpr std::uint32_t kAuraModResistance = 22;
constexpr std::uint32_t kAuraModStat = 29;
constexpr std::uint32_t kAuraModIncreaseHealth = 34;
constexpr std::uint32_t kAuraModIncreaseEnergy = 35;
constexpr std::uint32_t kAuraModDodgePercent = 49;
constexpr std::uint32_t kAuraModCritPercent = 52;
constexpr std::uint32_t kAuraModHitChance = 54;
constexpr std::uint32_t kAuraModSpellCritChanceSchool = 71;
constexpr std::uint32_t kAuraModPowerRegen = 85;
constexpr std::uint32_t kAuraModAttackPower = 99;
constexpr std::uint32_t kAuraModHealingDone = 135;

// Spell categories as UseConsumableAction::IsPotionLike (4) and IsFoodOrDrink (11 food, 59 drink) test them.
constexpr std::uint32_t kCategoryPotion = 4;
constexpr std::uint32_t kCategoryFood = 11;
constexpr std::uint32_t kCategoryDrink = 59;

constexpr std::size_t kMaxEffects = 3;

// Variant B (design F1, recommended): a derived flask (R1) or elixir (R3) needs a buff aura, not just any lasting aura. Without it
// (variant A) Flask of Petrification (self stun) and the utility elixirs (water breathing, detect, catseye, ...) would be used.
constexpr bool kBuffAuraFilter = true;

// The facts of one spell, after spell_mod / spell_effect_mod. known = false: no such spell (or no trigger in that effect).
struct SpellFacts
{
    bool known = false;
    std::uint32_t family = 0;        // SpellFamilyName
    std::uint32_t category = 0;      // Category
    std::uint32_t durationIndex = 0; // DurationIndex
    std::uint32_t targetA0 = 0;      // EffectImplicitTargetA[0]
    std::uint32_t effect[kMaxEffects] = {};
    std::uint32_t aura[kMaxEffects] = {}; // EffectApplyAuraName
    bool flaskAura = false;          // the spell id is one of UseConsumableAction::knownFlaskAuras
};

// One item spell slot with a spell id. trigger[j]: the spell's EffectTriggerSpell[j] when it is not 0 and not the spell itself
// (as UseConsumableAction::CollectPossibleAuraIds), else known = false.
struct ItemSpell
{
    std::uint32_t spellId = 0;
    std::uint32_t itemCategory = 0; // the item slot's SpellCategory
    SpellFacts spell;
    SpellFacts trigger[kMaxEffects];
};

inline bool IsBuffAura(std::uint32_t aura)
{
    switch (aura)
    {
    case kAuraModDamageDone:
    case kAuraModResistance:
    case kAuraModStat:
    case kAuraModIncreaseHealth:
    case kAuraModIncreaseEnergy:
    case kAuraModDodgePercent:
    case kAuraModCritPercent:
    case kAuraModHitChance:
    case kAuraModSpellCritChanceSchool:
    case kAuraModPowerRegen:
    case kAuraModAttackPower:
    case kAuraModHealingDone:
        return true;
    default:
        return false;
    }
}

// A lasting aura: a duration and an apply-aura effect with an aura (variant B: a buff aura).
inline bool HasLastingAura(SpellFacts const& spell, bool buffAuraFilter)
{
    if (!spell.known || !spell.durationIndex)
        return false;
    for (std::size_t j = 0; j < kMaxEffects; ++j)
    {
        if (spell.effect[j] == kEffectApplyAura && spell.aura[j] && (!buffAuraFilter || IsBuffAura(spell.aura[j])))
            return true;
    }
    return false;
}

// R4: a generic spell with exactly one effect, apply-aura MOD_STAT or MOD_RESISTANCE, cast on a friendly unit (elixirs, flasks
// and potions target the caster).
inline bool IsScrollSpell(SpellFacts const& spell)
{
    return spell.known && spell.family == kFamilyGeneric && spell.effect[0] == kEffectApplyAura && spell.effect[1] == 0 &&
        spell.effect[2] == 0 && (spell.aura[0] == kAuraModStat || spell.aura[0] == kAuraModResistance) &&
        spell.targetA0 == kTargetUnitFriend;
}

// The derived subclass of a subclass-0 consumable, rules in this order:
//   R0 an item spell slot with category 11 or 59 (IsFoodOrDrink): stays 0, food and drink take the old path;
//   R1 a spell or one of its triggers is a known flask aura with a lasting (buff) aura: flask;
//   R2 IsPotionLike (slot category 4 or spell category 4): potion (ClassifyConsumable returns false for it, as before);
//   R3 a spell of the potion family with a lasting (buff) aura: elixir;
//   R4 a scroll spell (IsScrollSpell): scroll;
//   R5 anything else: stays 0.
inline std::uint32_t DeriveSubClass(ItemSpell const* spells, std::size_t n, bool buffAuraFilter = kBuffAuraFilter)
{
    for (std::size_t i = 0; i < n; ++i)
    {
        if (spells[i].itemCategory == kCategoryFood || spells[i].itemCategory == kCategoryDrink)
            return kSubClassConsumable;
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        if (spells[i].spell.flaskAura && HasLastingAura(spells[i].spell, buffAuraFilter))
            return kSubClassFlask;
        for (std::size_t j = 0; j < kMaxEffects; ++j)
        {
            if (spells[i].trigger[j].flaskAura && HasLastingAura(spells[i].trigger[j], buffAuraFilter))
                return kSubClassFlask;
        }
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        if (spells[i].itemCategory == kCategoryPotion || (spells[i].spell.known && spells[i].spell.category == kCategoryPotion))
            return kSubClassPotion;
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        if (spells[i].spell.family == kFamilyPotion && HasLastingAura(spells[i].spell, buffAuraFilter))
            return kSubClassElixir;
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        if (IsScrollSpell(spells[i].spell))
            return kSubClassScroll;
    }

    return kSubClassConsumable;
}
}
