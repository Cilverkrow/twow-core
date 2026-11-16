#include "ConsumableKindPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

// twow-repo#333 T24 CLSFIX (OB-00 16.11.2026): the derived subclass of AiPlayerbot.Consumables.KindFromSpell on the spell data of
// real items. Every fixture is the effective spell row of the world DB (spell_template after spell_mod / spell_effect_mod; design
// dump clsfix-dump.tsv, ob40-482c-db tw_world, 16.11.2026); all these items have item class 0 and subclass 0.
namespace
{
using namespace ai::consumable_kind;

void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

SpellFacts Spell(std::uint32_t family, std::uint32_t category, std::uint32_t durationIndex, std::uint32_t targetA0,
    std::uint32_t effect0, std::uint32_t effect1, std::uint32_t effect2, std::uint32_t aura0, std::uint32_t aura1,
    std::uint32_t aura2, bool flaskAura = false)
{
    SpellFacts s;
    s.known = true;
    s.family = family;
    s.category = category;
    s.durationIndex = durationIndex;
    s.targetA0 = targetA0;
    s.effect[0] = effect0;
    s.effect[1] = effect1;
    s.effect[2] = effect2;
    s.aura[0] = aura0;
    s.aura[1] = aura1;
    s.aura[2] = aura2;
    s.flaskAura = flaskAura;
    return s;
}

ItemSpell Slot(std::uint32_t spellId, std::uint32_t itemCategory, SpellFacts const& spell)
{
    ItemSpell slot;
    slot.spellId = spellId;
    slot.itemCategory = itemCategory;
    slot.spell = spell;
    return slot;
}

std::uint32_t DeriveB(ItemSpell const& slot)
{
    return DeriveSubClass(&slot, 1, true);
}

std::uint32_t DeriveA(ItemSpell const& slot)
{
    return DeriveSubClass(&slot, 1, false);
}
}

int main()
{
    // The design's variant B is the default.
    Require(kBuffAuraFilter, "variant B (buff aura filter) is the default");

    // 13510 Flask of the Titans: spell 17626 (family 13, duration 367, apply aura 34, caster), a known flask aura -> flask.
    ItemSpell const titans = Slot(17626, 79, Spell(13, 0, 367, 1, 6, 0, 0, 34, 0, 0, true));
    Require(DeriveSubClass(&titans, 1) == kSubClassFlask, "Flask of the Titans -> flask");
    Require(DeriveA(titans) == kSubClassFlask, "Flask of the Titans -> flask (variant A)");

    // 3825 Elixir of Fortitude: spell 3593 "Health II" (family 13, category 0, duration 42, apply aura 34) -> elixir.
    ItemSpell const fortitude = Slot(3593, 79, Spell(13, 0, 42, 1, 6, 0, 0, 34, 0, 0));
    Require(DeriveSubClass(&fortitude, 1) == kSubClassElixir, "Elixir of Fortitude -> elixir");
    Require(DeriveA(fortitude) == kSubClassElixir, "Elixir of Fortitude -> elixir (variant A)");

    // 2289 Scroll of Strength II: spell 8119 (family 0, duration 30, one effect: apply aura 29 on a friendly unit, target 21) -> scroll.
    ItemSpell const strength = Slot(8119, 0, Spell(0, 0, 30, 21, 6, 0, 0, 29, 0, 0));
    Require(DeriveSubClass(&strength, 1) == kSubClassScroll, "Scroll of Strength II -> scroll");
    Require(DeriveA(strength) == kSubClassScroll, "Scroll of Strength II -> scroll (variant A)");

    // Real potions: 13446 / 929 / 118 healing potions (slot category 4, spell category 4, effect 10 heal) -> potion; the classifier
    // still returns false for a potion, as before.
    ItemSpell const majorHealing = Slot(17534, 4, Spell(13, 4, 0, 1, 10, 0, 0, 0, 0, 0));
    ItemSpell const healing = Slot(441, 4, Spell(13, 4, 0, 1, 10, 0, 0, 0, 0, 0));
    ItemSpell const minorHealing = Slot(439, 4, Spell(13, 4, 0, 1, 10, 0, 0, 0, 0, 0));
    Require(DeriveSubClass(&majorHealing, 1) == kSubClassPotion, "Major Healing Potion -> potion");
    Require(DeriveB(healing) == kSubClassPotion && DeriveA(healing) == kSubClassPotion, "Healing Potion -> potion");
    Require(DeriveB(minorHealing) == kSubClassPotion && DeriveA(minorHealing) == kSubClassPotion, "Minor Healing Potion -> potion");
    // 3826 Mighty Troll's Blood Potion: slot category 79 like the elixirs, but spell category 4 -> potion (R2 before R3, as before).
    ItemSpell const trollsBlood = Slot(3223, 79, Spell(13, 4, 42, 1, 6, 0, 0, 161, 0, 0));
    Require(DeriveB(trollsBlood) == kSubClassPotion && DeriveA(trollsBlood) == kSubClassPotion, "Mighty Troll's Blood -> potion");
    // IsPotionLike reads the slot category even without a spell row.
    ItemSpell unknownPotion;
    unknownPotion.spellId = 1;
    unknownPotion.itemCategory = 4;
    Require(DeriveB(unknownPotion) == kSubClassPotion, "slot category 4 without spell data -> potion");

    // Food and drink stay on the old path (R0): 4540 bread, 159 water, 724 buff food, 60983 Winter Veil Brew (slot 3 alone would be
    // a scroll spell).
    ItemSpell const bread = Slot(433, 11, Spell(0, 11, 85, 1, 6, 0, 0, 84, 0, 0));
    ItemSpell const water = Slot(430, 59, Spell(0, 59, 85, 1, 6, 0, 0, 85, 0, 0));
    ItemSpell pie = Slot(5005, 11, Spell(0, 11, 86, 1, 6, 6, 0, 84, 23, 0));
    pie.trigger[0] = Spell(0, 0, 347, 1, 6, 6, 0, 29, 29, 0);
    Require(DeriveB(bread) == kSubClassConsumable && DeriveA(bread) == kSubClassConsumable, "bread stays 0");
    Require(DeriveB(water) == kSubClassConsumable && DeriveA(water) == kSubClassConsumable, "water stays 0");
    Require(DeriveB(pie) == kSubClassConsumable && DeriveA(pie) == kSubClassConsumable, "Goretusk Liver Pie stays 0 (buff food)");
    ItemSpell const brew[3] = {
        Slot(11009, 11, Spell(0, 59, 0, 1, 100, 0, 0, 0, 0, 0)),
        Slot(1137, 0, Spell(0, 59, 9, 1, 6, 0, 0, 85, 0, 0)),
        Slot(8068, 0, Spell(0, 0, 30, 21, 6, 0, 0, 29, 0, 0)),
    };
    Require(DeriveSubClass(brew, 3) == kSubClassConsumable, "Winter Veil Brew stays 0 (food slot first)");
    Require(DeriveSubClass(brew + 2, 1) == kSubClassScroll, "its third slot alone is a scroll spell");

    // Variant A against B: 13506 Flask of Petrification (spell 17624: stun 12 + immunity 39, a known flask aura) and 5996 Elixir of
    // Water Breathing (spell 7178, aura 82) have a lasting aura but no buff aura.
    ItemSpell const petrification = Slot(17624, 79, Spell(13, 37, 3, 1, 6, 6, 79, 12, 39, 0, true));
    ItemSpell const waterBreathing = Slot(7178, 79, Spell(13, 99, 30, 1, 6, 0, 0, 82, 0, 0));
    Require(DeriveB(petrification) == kSubClassConsumable, "Flask of Petrification: B stays 0");
    Require(DeriveA(petrification) == kSubClassFlask, "Flask of Petrification: A flask");
    Require(DeriveB(waterBreathing) == kSubClassConsumable, "Elixir of Water Breathing: B stays 0");
    Require(DeriveA(waterBreathing) == kSubClassElixir, "Elixir of Water Breathing: A elixir");

    // 13512 Flask of Supreme Power (spell 17628, aura 13 spell damage) -> flask in both variants.
    ItemSpell const supremePower = Slot(17628, 79, Spell(13, 99, 367, 1, 6, 0, 0, 13, 0, 0, true));
    Require(DeriveB(supremePower) == kSubClassFlask && DeriveA(supremePower) == kSubClassFlask, "Flask of Supreme Power -> flask");
    // A known flask aura that only comes from a trigger counts as well (as the flask branch's CollectPossibleAuraIds).
    ItemSpell viaTrigger = Slot(1, 0, Spell(0, 0, 0, 1, 64, 0, 0, 0, 0, 0));
    viaTrigger.trigger[0] = Spell(13, 0, 367, 1, 6, 0, 0, 34, 0, 0, true);
    Require(DeriveB(viaTrigger) == kSubClassFlask, "flask aura from a trigger -> flask");

    // 13445 Elixir of Superior Defense (spell 11348, aura 22 armor) is an elixir here; the elixir branch rejects it as protection.
    ItemSpell const superiorDefense = Slot(11348, 79, Spell(13, 0, 42, 1, 6, 0, 0, 22, 0, 0));
    Require(DeriveB(superiorDefense) == kSubClassElixir && DeriveA(superiorDefense) == kSubClassElixir, "Superior Defense -> elixir");

    // Not derived: 65016 Scroll of Thorns (family 7), 21151 Rumsey Rum (two effects; the alcohol list runs before the subclass).
    ItemSpell const thorns = Slot(21335, 0, Spell(7, 0, 6, 21, 6, 0, 0, 15, 0, 0));
    ItemSpell rum = Slot(25804, 0, Spell(0, 0, 347, 1, 6, 64, 0, 29, 0, 0));
    rum.trigger[1] = Spell(0, 59, 0, 1, 100, 0, 0, 0, 0, 0);
    Require(DeriveB(thorns) == kSubClassConsumable && DeriveA(thorns) == kSubClassConsumable, "Scroll of Thorns stays 0");
    Require(DeriveB(rum) == kSubClassConsumable && DeriveA(rum) == kSubClassConsumable, "Rumsey Rum stays 0");

    // A scroll needs a friendly-unit target: the same spell on the caster stays 0.
    ItemSpell const selfStrength = Slot(8119, 0, Spell(0, 0, 30, 1, 6, 0, 0, 29, 0, 0));
    Require(DeriveB(selfStrength) == kSubClassConsumable, "scroll spell on the caster stays 0");
    // No spell slots, unknown spells: 0.
    Require(DeriveSubClass(nullptr, 0) == kSubClassConsumable, "no item spells -> 0");
    ItemSpell unknown;
    unknown.spellId = 1;
    Require(DeriveB(unknown) == kSubClassConsumable, "unknown spell -> 0");

    std::cout << "consumable_kind_policy_tests: OK\n";
    return 0;
}
