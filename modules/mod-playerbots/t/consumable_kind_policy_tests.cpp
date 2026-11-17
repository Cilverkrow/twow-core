#include "ConsumableKindPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

// twow-repo#333 T24 CLSFIX (OB-00 16.11.2026): the derived subclass of AiPlayerbot.Consumables.KindFromSpell on the spell data of
// real items. Every fixture is the effective spell row of the world DB (spell_template after spell_mod / spell_effect_mod; design
// dump clsfix-dump.tsv, ob40-482c-db tw_world, 16.11.2026; the review fixtures of 17.11. checked again there); all these items have
// item class 0 and subclass 0. Exception: the fixtures marked "Synthetic" are not DB rows; each one isolates one rule clause that no
// real item isolates.
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
    // 1478 Scroll of Protection II: spell 8094 "Armor" (family 0, duration 30, one effect: apply aura 22 MOD_RESISTANCE, target 21)
    // -> scroll. The MOD_RESISTANCE half of R4: the five newly classified scrolls (Scroll of Protection I-IV, Spirit Armor V).
    ItemSpell const protection = Slot(8094, 0, Spell(0, 0, 30, 21, 6, 0, 0, 22, 0, 0));
    Require(DeriveB(protection) == kSubClassScroll && DeriveA(protection) == kSubClassScroll, "Scroll of Protection II -> scroll");

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
    pie.trigger[1] = Spell(0, 0, 347, 1, 6, 6, 0, 29, 29, 0); // 19706 Well Fed, the trigger of effect 1 (aura 23)
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
    // Synthetic: the drink half of R0. The water slot (59) first, then the Scroll of Strength II slot: stays 0, like the food slot of
    // Winter Veil Brew (R0 reads every slot, so the order does not matter).
    ItemSpell const drinkFirst[2] = {
        Slot(430, 59, Spell(0, 59, 85, 1, 6, 0, 0, 85, 0, 0)),
        Slot(8119, 0, Spell(0, 0, 30, 21, 6, 0, 0, 29, 0, 0)),
    };
    Require(DeriveSubClass(drinkFirst, 2) == kSubClassConsumable, "drink slot first stays 0");

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

    // Every buff aura of variant B (IsBuffAura) with one fixture whose only buff aura it is. 13, 22 and 34: Supreme Power, Superior
    // Defense, Titans / Fortitude above. Real items:
    // 13511 Flask of Distilled Wisdom: spell 17627 (family 13, duration 367, apply aura 35 MOD_INCREASE_ENERGY mana) -> flask.
    ItemSpell const wisdom = Slot(17627, 79, Spell(13, 0, 367, 1, 6, 0, 0, 35, 0, 0, true));
    Require(DeriveB(wisdom) == kSubClassFlask, "Flask of Distilled Wisdom -> flask");
    // 9206 Elixir of Giants: spell 11405 (family 13, duration 42, apply aura 29 MOD_STAT) -> elixir.
    ItemSpell const giants = Slot(11405, 79, Spell(13, 0, 42, 1, 6, 0, 0, 29, 0, 0));
    Require(DeriveB(giants) == kSubClassElixir, "Elixir of Giants (aura 29) -> elixir");
    // 22192 Bloodkelp Elixir of Dodging: spell 27653 (family 13, duration 30, apply aura 49 MOD_DODGE_PERCENT) -> elixir.
    ItemSpell const dodging = Slot(27653, 79, Spell(13, 0, 30, 1, 6, 0, 0, 49, 0, 0));
    Require(DeriveB(dodging) == kSubClassElixir, "Bloodkelp Elixir of Dodging (aura 49) -> elixir");
    // 42017 Sapta of Harmony: spell 52783 (family 13, category 99, duration 40, apply aura 71 MOD_SPELL_CRIT_CHANCE_SCHOOL) -> elixir.
    ItemSpell const harmony = Slot(52783, 99, Spell(13, 99, 40, 1, 6, 0, 0, 71, 0, 0));
    Require(DeriveB(harmony) == kSubClassElixir, "Sapta of Harmony (aura 71) -> elixir");
    // 12820 Winterfall Firewater: spell 17038 (family 13, duration 40, apply aura 99 MOD_ATTACK_POWER and 61 MOD_SCALE, not a buff
    // aura) -> elixir.
    ItemSpell const firewater = Slot(17038, 0, Spell(13, 0, 40, 1, 6, 6, 0, 99, 61, 0));
    Require(DeriveB(firewater) == kSubClassElixir, "Winterfall Firewater (aura 99) -> elixir");
    // Synthetic: no item spell reaches R1 / R3 with 52, 54, 85 or 135 as its only buff aura (Elixir of the Mongoose also has 29, the
    // aura-85 spell 24363 has spell category 4 and is a potion at R2) -> the Elixir of Fortitude row with that aura.
    Require(DeriveB(Slot(1, 79, Spell(13, 0, 42, 1, 6, 0, 0, 52, 0, 0))) == kSubClassElixir, "synthetic aura 52 -> elixir");
    Require(DeriveB(Slot(1, 79, Spell(13, 0, 42, 1, 6, 0, 0, 54, 0, 0))) == kSubClassElixir, "synthetic aura 54 -> elixir");
    Require(DeriveB(Slot(1, 79, Spell(13, 0, 42, 1, 6, 0, 0, 85, 0, 0))) == kSubClassElixir, "synthetic aura 85 -> elixir");
    Require(DeriveB(Slot(1, 79, Spell(13, 0, 42, 1, 6, 0, 0, 135, 0, 0))) == kSubClassElixir, "synthetic aura 135 -> elixir");

    // Synthetic: R1 and R3 need a duration and the aura on an apply-aura effect (no item spell of the potion family lacks either).
    ItemSpell const noDuration = Slot(1, 79, Spell(13, 0, 0, 1, 6, 0, 0, 34, 0, 0));
    ItemSpell const flaskNoDuration = Slot(17626, 79, Spell(13, 0, 0, 1, 6, 0, 0, 34, 0, 0, true));
    ItemSpell const notApplyAura = Slot(1, 79, Spell(13, 0, 42, 1, 10, 0, 0, 34, 0, 0));
    Require(DeriveB(noDuration) == kSubClassConsumable && DeriveA(noDuration) == kSubClassConsumable,
        "potion family without duration stays 0");
    Require(DeriveB(flaskNoDuration) == kSubClassConsumable, "flask aura without duration stays 0");
    Require(DeriveB(notApplyAura) == kSubClassConsumable, "aura on a non apply-aura effect stays 0");

    // Not derived: 65016 Scroll of Thorns (family 7), 21151 Rumsey Rum (two effects; the alcohol list runs before the subclass).
    ItemSpell const thorns = Slot(21335, 0, Spell(7, 0, 6, 21, 6, 0, 0, 15, 0, 0));
    ItemSpell rum = Slot(25804, 0, Spell(0, 0, 347, 1, 6, 64, 0, 29, 0, 0));
    rum.trigger[1] = Spell(0, 59, 0, 1, 100, 0, 0, 0, 0, 0);
    Require(DeriveB(thorns) == kSubClassConsumable && DeriveA(thorns) == kSubClassConsumable, "Scroll of Thorns stays 0");
    Require(DeriveB(rum) == kSubClassConsumable && DeriveA(rum) == kSubClassConsumable, "Rumsey Rum stays 0");
    // R4 needs exactly one effect and family GENERIC. 21267 Toasting Goblet: spell 26008 "Toast" (family 0, duration 30, apply aura 29
    // on target 21, but a second effect 100) stays 0.
    ItemSpell const toast = Slot(26008, 0, Spell(0, 0, 30, 21, 6, 100, 0, 29, 0, 0));
    Require(DeriveB(toast) == kSubClassConsumable && DeriveA(toast) == kSubClassConsumable, "Toasting Goblet (second effect) stays 0");
    // Synthetic: Scroll of Strength II with a third effect, and with family 7.
    ItemSpell const thirdEffect = Slot(1, 0, Spell(0, 0, 30, 21, 6, 0, 6, 29, 0, 29));
    ItemSpell const otherFamily = Slot(1, 0, Spell(7, 0, 30, 21, 6, 0, 0, 29, 0, 0));
    Require(DeriveB(thirdEffect) == kSubClassConsumable && DeriveA(thirdEffect) == kSubClassConsumable,
        "scroll spell with a third effect stays 0");
    Require(DeriveB(otherFamily) == kSubClassConsumable && DeriveA(otherFamily) == kSubClassConsumable,
        "scroll spell of another family stays 0");

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
