#include "PotionFindPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#333 T24 POTFIX: the subclass-0 potion rule of FindPotionVisitor (AiPlayerbot.PotionFind.BySpell). The fixtures are the
// item spell facts of test DB C (ob40-482c-db tw_world, read-only SELECT 16.11.2026, after spell_mod / spell_effect_mod): item slot
// SpellCategory, spell Category, Effect and EffectMiscValue of spellid_1.
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

SlotFacts Slot(std::uint32_t itemCategory, std::uint32_t spellCategory, std::uint32_t e0, std::uint32_t e1 = 0, std::uint32_t e2 = 0,
    std::int32_t m0 = 0, std::int32_t m1 = 0, std::int32_t m2 = 0)
{
    SlotFacts slot;
    slot.known = true;
    slot.itemCategory = itemCategory;
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
    Require(kEffectHeal == 10 && kEffectEnergize == 30 && kPowerMana == 0 && kCategoryPotion == 4, "constants");

    // Healing potions: 118 Minor (439), 929 (441), 13446 Major (17534): slot and spell category 4, HEAL.
    SlotFacts const minorHealing = Slot(4, 4, kHeal);
    SlotFacts const healing = Slot(4, 4, kHeal);
    SlotFacts const majorHealing = Slot(4, 4, kHeal);
    Require(Heals(minorHealing) && Heals(healing) && Heals(majorHealing), "118 / 929 / 13446 are healing potions");
    Require(!Mana(minorHealing) && !Mana(majorHealing), "a healing potion is no mana potion");

    // Mana potions: 2455 Minor (437), 13444 Major (17531): category 4, ENERGIZE mana.
    SlotFacts const minorMana = Slot(4, 4, kEnergize);
    SlotFacts const majorMana = Slot(4, 4, kEnergize);
    Require(Mana(minorMana) && Mana(majorMana), "2455 / 13444 are mana potions");
    Require(!Heals(minorMana) && !Heals(majorMana), "a mana potion is no healing potion");

    // Both: 2456 Minor Rejuvenation Potion (2370), 18253 Major Rejuvenation Potion (22729): ENERGIZE + HEAL.
    SlotFacts const minorRejuvenation = Slot(4, 4, kEnergize, kHeal);
    SlotFacts const majorRejuvenation = Slot(4, 4, kEnergize, kHeal);
    Require(Heals(minorRejuvenation) && Mana(minorRejuvenation), "2456 heals and restores mana");
    Require(Heals(majorRejuvenation) && Mana(majorRejuvenation), "18253 heals and restores mana");

    // The spell category alone counts (item slot 1153): 11951 Whipper Root Tuber (15700), 23683 Crystal Flake Throat Lozenge (30018),
    // 14894 Lily Root (18832, HEAL + ENERGIZE). The draughts: see the battleground block below.
    Require(Heals(Slot(1153, 4, kHeal)), "11951 / 23683 share the potion cooldown");
    Require(Heals(Slot(1153, 4, kHeal, kEnergize)) && Mana(Slot(1153, 4, kHeal, kEnergize)), "14894 Lily Root");
    // And the item slot category alone (no such row on this DB, as IsPotionLike tests either).
    Require(Heals(Slot(4, 0, kHeal)), "slot category 4 alone");

    // Not a healing potion: bandage 14529 (18608, slot 150, aura), bread 4540 (433, 11/11, aura), water 159 (430, 59/59, aura),
    // Elixir of Fortitude 3825 (3593, 79/0, aura 34), Flask of the Titans 13510 (17626, 79/0, aura 34).
    SlotFacts const bandage = Slot(150, 0, kAura);
    SlotFacts const bread = Slot(11, 11, kAura);
    SlotFacts const water = Slot(59, 59, kAura);
    SlotFacts const elixirOfFortitude = Slot(79, 0, kAura);
    SlotFacts const flaskOfTheTitans = Slot(79, 0, kAura);
    Require(!Heals(bandage) && !Heals(bread) && !Heals(water) && !Heals(elixirOfFortitude) && !Heals(flaskOfTheTitans),
        "bandage / bread / water / elixir / flask are no healing potions");
    Require(!Mana(bandage) && !Mana(bread) && !Mana(water) && !Mana(elixirOfFortitude) && !Mana(flaskOfTheTitans),
        "bandage / bread / water / elixir / flask are no mana potions");

    // HEAL outside the potion cooldown: 5512 Minor Healthstone (6262, 1153/30), 1401 Green Tea Leaf (833, 11/0, HEAL + aura),
    // 15723 Tea with Sugar (19199, 1172/0, HEAL + ENERGIZE).
    Require(!Heals(Slot(1153, 30, kHeal)), "healthstone: own action, own cooldown");
    Require(!Heals(Slot(11, 0, kHeal, kAura)), "Green Tea Leaf: food category");
    Require(!Heals(Slot(1172, 0, kHeal, kEnergize)) && !Mana(Slot(1172, 0, kHeal, kEnergize)), "Tea with Sugar: own cooldown");

    // ENERGIZE that is no mana potion: 8008 Mana Ruby (10058, 1153/30, mana), 5631 Rage Potion (6612, 4/4, power 1),
    // 13442 Mighty Rage Potion (17528, 4/4, ENERGIZE rage + aura), 7676 Thistle Tea (9512, 1153/60, power 3 energy).
    Require(!Mana(Slot(1153, 30, kEnergize)), "mana gem: own cooldown");
    Require(!Mana(Slot(4, 4, kEnergize, 0, 0, 1)), "Rage Potion: rage, not mana");
    Require(!Mana(Slot(4, 4, kEnergize, kAura, 0, 1)), "Mighty Rage Potion: rage, not mana");
    Require(!Mana(Slot(1153, 60, kEnergize, 0, 0, 3)), "Thistle Tea: energy, not mana");
    // The mana check is per effect: ENERGIZE rage next to ENERGIZE mana is a mana potion.
    Require(Mana(Slot(4, 4, kEnergize, kEnergize, 0, 1, 0)), "the mana effect in index 1");

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
        SlotFacts only[5] = { unknown };
        Require(!AcceptsVanillaPotion(0, 0, only, 5, kHeal, false), "an unknown spell with slot category 4 is no potion");
        Require(!AcceptsVanillaPotion(0, 0, slots, 0, kHeal, false), "no slots");
    }

    // Battleground-only spells (the draughts 17348/17349 heal, 17351/17352 mana; spells 21393-21396 with AttributesEx3 0x800):
    // a potion only inside a battleground, elsewhere the cast fails (SPELL_FAILED_ONLY_BATTLEGROUNDS).
    {
        SlotFacts majorHealingDraught = Slot(4, 4, kHeal);
        majorHealingDraught.battlegroundOnly = true;
        SlotFacts majorManaDraught = Slot(4, 4, kEnergize);
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
        SlotFacts slots[5] = { Slot(4, 4, kAura), Slot(0, 0, kHeal) };
        Require(!AcceptsVanillaPotion(0, 0, slots, 5, kHeal, false), "category on slot 1, HEAL on slot 2");
    }

    return 0;
}
