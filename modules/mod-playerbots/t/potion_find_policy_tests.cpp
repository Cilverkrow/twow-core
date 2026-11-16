#include "PotionFindPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#333 T24 POTFIX: the subclass-0 potion rule of FindPotionVisitor (AiPlayerbot.PotionFind.BySpell). The fixtures are the
// item spell facts of test DB C (ob40-482c-db tw_world, read-only SELECT 16.11.2026, after spell_mod / spell_effect_mod): item slot
// SpellCooldown, SpellCategory and SpellCategoryCooldown, spell Category, Effect and EffectMiscValue of spellid_1.
namespace
{
using ai::potion_find::SlotFacts;

void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

SlotFacts Slot(std::int32_t itemCooldown, std::uint32_t itemCategory, std::int32_t itemCategoryCooldown, std::uint32_t spellCategory,
    std::uint32_t e0, std::uint32_t e1 = 0, std::uint32_t e2 = 0, std::int32_t m0 = 0, std::int32_t m1 = 0, std::int32_t m2 = 0)
{
    SlotFacts slot;
    slot.known = true;
    slot.itemCooldown = itemCooldown;
    slot.itemCategory = itemCategory;
    slot.itemCategoryCooldown = itemCategoryCooldown;
    slot.spellCategory = spellCategory;
    slot.effect[0] = e0;
    slot.effect[1] = e1;
    slot.effect[2] = e2;
    slot.misc[0] = m0;
    slot.misc[1] = m1;
    slot.misc[2] = m2;
    return slot;
}

constexpr std::uint32_t kHeal = ai::potion_find::kEffectHeal;
constexpr std::uint32_t kEnergize = ai::potion_find::kEffectEnergize;
constexpr std::uint32_t kAura = 6; // SPELL_EFFECT_APPLY_AURA

// An item of class 0 / subclass 0 with this spell in slot 1 and the other four slots empty.
bool Heals(SlotFacts const& slot1)
{
    SlotFacts slots[5] = { slot1 };
    return ai::potion_find::AcceptsVanillaPotion(0, 0, slots, 5, kHeal, false);
}

bool Mana(SlotFacts const& slot1)
{
    SlotFacts slots[5] = { slot1 };
    return ai::potion_find::AcceptsVanillaPotion(0, 0, slots, 5, kEnergize, false);
}
}

int main()
{
    using namespace ai::potion_find;

    // Mirrors of the core enums (pinned against the core by the source contract).
    Require(kEffectHeal == 10 && kEffectEnergize == 30 && kPowerMana == 0 && kCategoryPotion == 4 && kMaxEffects == 3, "constants");

    // Healing potions: 118 Minor (439), 929 (441), 13446 Major (17534): slot 120000 / 4 / 120000, spell category 4, HEAL.
    SlotFacts const minorHealing = Slot(120000, 4, 120000, 4, kHeal);
    SlotFacts const healing = Slot(120000, 4, 120000, 4, kHeal);
    SlotFacts const majorHealing = Slot(120000, 4, 120000, 4, kHeal);
    Require(Heals(minorHealing) && Heals(healing) && Heals(majorHealing), "118 / 929 / 13446 are healing potions");
    Require(!Mana(minorHealing) && !Mana(majorHealing), "a healing potion is no mana potion");

    // Mana potions: 2455 Minor (437), 13444 Major (17531): category 4, ENERGIZE mana.
    SlotFacts const minorMana = Slot(120000, 4, 120000, 4, kEnergize);
    SlotFacts const majorMana = Slot(120000, 4, 120000, 4, kEnergize);
    Require(Mana(minorMana) && Mana(majorMana), "2455 / 13444 are mana potions");
    Require(!Heals(minorMana) && !Heals(majorMana), "a mana potion is no healing potion");

    // Both: 2456 Minor Rejuvenation Potion (2370), 18253 Major Rejuvenation Potion (22729): ENERGIZE + HEAL.
    SlotFacts const minorRejuvenation = Slot(120000, 4, 120000, 4, kEnergize, kHeal);
    SlotFacts const majorRejuvenation = Slot(120000, 4, 120000, 4, kEnergize, kHeal);
    Require(Heals(minorRejuvenation) && Mana(minorRejuvenation), "2456 heals and restores mana");
    Require(Heals(majorRejuvenation) && Mana(majorRejuvenation), "18253 heals and restores mana");

    // Moonshine 1072 (438) and Mug of Shimmer Stout 3087 (29236): no slot SpellCooldown (-1), but a slot category cooldown, so the
    // slot category 4 decides.
    Require(Mana(Slot(-1, 4, 120000, 4, kEnergize)), "1072 / 3087: slot category 4 with only a category cooldown");

    // Spell Category 4 but slot category 1153 with a slot cooldown: the core puts the cast on the healthstone / mana gem cooldown
    // (Unit::AddSpellAndCategoryCooldowns), so no potion: 11951 Whipper Root Tuber (15700), 23683 Crystal Flake Throat Lozenge
    // (30018), 11952 Night Dragon's Breath (15701, ENERGIZE + HEAL), 14894 Lily Root (18832, HEAL + ENERGIZE).
    Require(!Heals(Slot(-1, 1153, 120000, 4, kHeal)), "11951 Whipper Root Tuber: healthstone cooldown");
    Require(!Heals(Slot(0, 1153, 120000, 4, kHeal)), "23683 Lozenge: healthstone cooldown");
    Require(!Heals(Slot(-1, 1153, 120000, 4, kEnergize, kHeal)) && !Mana(Slot(-1, 1153, 120000, 4, kEnergize, kHeal)),
        "11952 Night Dragon's Breath: healthstone cooldown");
    Require(!Heals(Slot(0, 1153, 120000, 4, kHeal, kEnergize)) && !Mana(Slot(0, 1153, 120000, 4, kHeal, kEnergize)),
        "14894 Lily Root: healthstone cooldown");
    // Only when the slot sets no cooldown at all (both -1) does the spell Category decide (no HEAL / ENERGIZE row of that kind on
    // this DB; 15 class-0 slots with both -1, none heals or restores mana).
    Require(Heals(Slot(-1, 0, -1, 4, kHeal)), "no slot cooldown: spell category 4 decides");
    Require(!Heals(Slot(-1, 4, -1, 0, kHeal)), "no slot cooldown: slot category 4 does not count");
    Require(Heals(Slot(0, 4, -1, 0, kHeal)), "slot cooldown 0 set: slot category 4 decides");

    // Not a healing potion: bandage 14529 (18608, 0 / 150 / 0, aura), bread 4540 (433, 0 / 11 / 1000, aura), water 159 (430,
    // 0 / 59 / 1000, aura), Elixir of Fortitude 3825 (3593, 0 / 79 / 3000, aura), Flask of the Titans 13510 (17626, 0 / 79 / 3000).
    SlotFacts const bandage = Slot(0, 150, 0, 0, kAura);
    SlotFacts const bread = Slot(0, 11, 1000, 11, kAura);
    SlotFacts const water = Slot(0, 59, 1000, 59, kAura);
    SlotFacts const elixirOfFortitude = Slot(0, 79, 3000, 0, kAura);
    SlotFacts const flaskOfTheTitans = Slot(0, 79, 3000, 0, kAura);
    Require(!Heals(bandage) && !Heals(bread) && !Heals(water) && !Heals(elixirOfFortitude) && !Heals(flaskOfTheTitans),
        "bandage / bread / water / elixir / flask are no healing potions");
    Require(!Mana(bandage) && !Mana(bread) && !Mana(water) && !Mana(elixirOfFortitude) && !Mana(flaskOfTheTitans),
        "bandage / bread / water / elixir / flask are no mana potions");

    // HEAL outside the potion cooldown: 5512 Minor Healthstone (6262, 0 / 1153 / 120000, spell 30), 1401 Green Tea Leaf (833,
    // 0 / 11 / 1000, HEAL + aura), 15723 Tea with Sugar (19199, -1 / 1172 / 120000, HEAL + ENERGIZE).
    Require(!Heals(Slot(0, 1153, 120000, 30, kHeal)), "healthstone: own action, own cooldown");
    Require(!Heals(Slot(0, 11, 1000, 0, kHeal, kAura)), "Green Tea Leaf: food category");
    Require(!Heals(Slot(-1, 1172, 120000, 0, kHeal, kEnergize)) && !Mana(Slot(-1, 1172, 120000, 0, kHeal, kEnergize)),
        "Tea with Sugar: own cooldown");

    // ENERGIZE that is no mana potion: 8008 Mana Ruby (10058, 0 / 1153 / 120000, spell 30, mana), 5631 Rage Potion (6612, potion
    // slot, power 1), 13442 Mighty Rage Potion (17528, ENERGIZE rage + aura), 7676 Thistle Tea (9512, 300000 / 1153 / 120000,
    // spell 60, power 3 energy).
    Require(!Mana(Slot(0, 1153, 120000, 30, kEnergize)), "mana gem: own cooldown");
    Require(!Mana(Slot(120000, 4, 120000, 4, kEnergize, 0, 0, 1)), "Rage Potion: rage, not mana");
    Require(!Mana(Slot(120000, 4, 120000, 4, kEnergize, kAura, 0, 1)), "Mighty Rage Potion: rage, not mana");
    Require(!Mana(Slot(300000, 1153, 120000, 60, kEnergize, 0, 0, 3)), "Thistle Tea: energy, not mana");
    // The mana check is per effect: ENERGIZE rage next to ENERGIZE mana is a mana potion.
    Require(Mana(Slot(120000, 4, 120000, 4, kEnergize, kEnergize, 0, 1, 0)), "the mana effect in index 1");
    // Every effect index counts, the last one too (no such row on this DB).
    Require(Heals(Slot(120000, 4, 120000, 4, kAura, kAura, kHeal)), "the HEAL effect in index 2");
    Require(Mana(Slot(120000, 4, 120000, 4, kAura, kAura, kEnergize)), "the ENERGIZE effect in index 2");
    Require(!Mana(Slot(120000, 4, 120000, 4, kAura, kAura, kEnergize, 0, 0, 1)), "the ENERGIZE effect in index 2, rage");

    // Only class 0 / subclass 0: a recipe (class 9, "Recipe: Major Healing Potion" 13480) and subclass 1 / 3 (the old loop).
    {
        SlotFacts slots[5] = { majorHealing };
        Require(!AcceptsVanillaPotion(9, 6, slots, 5, kHeal, false), "class 9 recipe");
        Require(!AcceptsVanillaPotion(15, 0, slots, 5, kHeal, false), "class 15 Craftsman's Writ");
        Require(!AcceptsVanillaPotion(0, 1, slots, 5, kHeal, false), "subclass 1: the old loop decides");
        Require(!AcceptsVanillaPotion(0, 3, slots, 5, kHeal, false), "subclass 3: the old loop decides");
        Require(AcceptsVanillaPotion(0, 0, slots, 5, kHeal, false), "subclass 0");
    }

    // A slot without a known spell is skipped (the old loop returned false there): the potion spell in slot 2 counts.
    {
        SlotFacts slots[5];
        slots[1] = majorHealing;
        Require(AcceptsVanillaPotion(0, 0, slots, 5, kHeal, false), "empty slot 1, potion in slot 2");
        SlotFacts unknown;
        unknown.itemCategory = 4;
        unknown.itemCooldown = 120000;
        SlotFacts only[5] = { unknown };
        Require(!AcceptsVanillaPotion(0, 0, only, 5, kHeal, false), "an unknown spell with slot category 4 is no potion");
        Require(!AcceptsVanillaPotion(0, 0, slots, 0, kHeal, false), "no slots");
    }

    // Battleground-only spells (the draughts 17348/17349 heal, 17351/17352 mana; spells 21393-21396 with AttributesEx3 0x800,
    // slot 120000 / 4 / 300000): a potion only inside a battleground, elsewhere the cast fails (SPELL_FAILED_ONLY_BATTLEGROUNDS).
    {
        SlotFacts majorHealingDraught = Slot(120000, 4, 300000, 4, kHeal);
        majorHealingDraught.battlegroundOnly = true;
        SlotFacts majorManaDraught = Slot(120000, 4, 300000, 4, kEnergize);
        majorManaDraught.battlegroundOnly = true;
        SlotFacts heal[5] = { majorHealingDraught };
        SlotFacts mana[5] = { majorManaDraught };
        Require(!AcceptsVanillaPotion(0, 0, heal, 5, kHeal, false), "17348 draught outside a battleground: no potion");
        Require(AcceptsVanillaPotion(0, 0, heal, 5, kHeal, true), "17348 draught in a battleground: a healing potion");
        Require(!AcceptsVanillaPotion(0, 0, mana, 5, kEnergize, false), "17351 draught outside a battleground: no potion");
        Require(AcceptsVanillaPotion(0, 0, mana, 5, kEnergize, true), "17351 draught in a battleground: a mana potion");
        SlotFacts regular[5] = { majorHealing };
        Require(AcceptsVanillaPotion(0, 0, regular, 5, kHeal, true), "a regular potion also in a battleground");
    }

    // Category and effect must sit on the same slot.
    {
        SlotFacts slots[5] = { Slot(120000, 4, 120000, 4, kAura), Slot(0, 0, 0, 0, kHeal) };
        Require(!AcceptsVanillaPotion(0, 0, slots, 5, kHeal, false), "category on slot 1, HEAL on slot 2");
    }

    return 0;
}
