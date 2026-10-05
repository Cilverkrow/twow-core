#pragma once

#include <cstdint>
#include <ctime>

// twow-repo#485 (owner decision 9): roster bots on their own use bandages, healing and mana
// potions from their bags - no item cheat consumption. Pure rules, std only; included only by
// the .cpp files that use them (UseItemAction.cpp, ItemUsageValue.cpp, ProfessionUseTriggers.cpp).
namespace ai::consumables
{
enum class Kind
{
    None,
    Bandage,
    Healing, // healing potions and other heal items (healthstones, tubers)
    Mana     // mana potions (and dark runes)
};

// A bandage is a bandage even when its spell heals; heal before energize like ItemUsageValue.
inline Kind Classify(bool healing, bool mana, bool bandage)
{
    if (bandage)
        return Kind::Bandage;
    if (healing)
        return Kind::Healing;
    if (mana)
        return Kind::Mana;
    return Kind::None;
}

inline char const* Name(Kind kind)
{
    switch (kind)
    {
    case Kind::Bandage: return "bandage";
    case Kind::Healing: return "healing";
    case Kind::Mana: return "mana";
    default: return "none";
    }
}

// The "inventory items" query of the kind (the names ItemUsageValue already uses).
inline char const* InventoryQuery(Kind kind)
{
    switch (kind)
    {
    case Kind::Bandage: return "bandage";
    case Kind::Healing: return "healing potion";
    case Kind::Mana: return "mana potion";
    default: return "";
    }
}

// UseReal = 0 (default) or not a persistent roster bot on its own: the legacy path (under the
// item cheat a potion or bandage is cast without an item). Otherwise only items from the bags.
inline bool UsesReal(bool useReal, bool rosterBotOnItsOwn, Kind kind)
{
    return useReal && rosterBotOnItsOwn && kind != Kind::None;
}

// Extra gate on top of the strategy trigger (critical health / low mana): use at or below
// capPct percent. 100 (default) = no extra gate, the trigger alone decides.
inline bool AtOrBelow(float pct, std::uint32_t capPct)
{
    return capPct >= 100 || pct <= float(capPct);
}

// Stock of usable potions and bandages of a roster bot under UseReal.
enum class Stock
{
    Legacy, // the old rules decide (sold under the item cheat)
    Buy,    // ITEM_USAGE_USE: buy up to one stack (consumables budget)
    Keep    // ITEM_USAGE_KEEP: neither sold nor bought
};

// Buying stops at one stack (BuyAction), so a keep limit below two stacks would sell what was just
// bought and buy it again. The limit is therefore never below two stacks.
inline float KeepLimit(std::uint32_t keepStacks)
{
    return keepStacks < 2 ? 2.0f : float(keepStacks);
}

// Purchase under RosterConsumables.Buy (owner decision 04.10): only vendor goods (an item some
// vendor sells, npc_vendor) and never a bandage - bandages are only self-made (First Aid).
// The result is the buy flag for Decide; false turns a Buy into a Keep.
inline bool BuyAllowed(bool buy, Kind kind, bool soldByVendor)
{
    return buy && kind != Kind::Bandage && kind != Kind::None && soldByVendor;
}

// appropriate: the bot can use it now (level, skill, mana user). better: stacks of a better item
// of the same kind - then this one is outclassed and goes the old way (sold).
inline Stock Decide(bool appropriate, float ownStacks, float betterStacks, std::uint32_t keepStacks, bool buy)
{
    if (!appropriate)
        return Stock::Legacy;
    if (betterStacks >= 1.0f)
        return Stock::Legacy;
    if (ownStacks + betterStacks < 1.0f)
        return buy ? Stock::Buy : Stock::Keep;
    if (ownStacks < KeepLimit(keepStacks))
        return Stock::Keep;
    return Stock::Legacy;
}

// Throttled [Consumable] line: the first use logs, later ones are counted until the cooldown ran.
inline bool TraceDue(std::time_t now, std::time_t last, std::uint32_t cooldownSeconds)
{
    return last == 0 || now >= last + std::time_t(cooldownSeconds);
}
}
