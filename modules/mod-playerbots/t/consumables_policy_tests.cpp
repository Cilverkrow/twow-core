#include "ConsumablesPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::consumables;

    // Kind: a bandage stays a bandage, heal before energize, other items are none.
    Require(Classify(true, false, true) == Kind::Bandage, "a healing bandage is a bandage");
    Require(Classify(true, true, false) == Kind::Healing, "heal before energize");
    Require(Classify(false, true, false) == Kind::Mana, "energize = mana");
    Require(Classify(false, false, false) == Kind::None, "food, elixirs, scrolls: none");
    Require(std::strcmp(Name(Kind::Bandage), "bandage") == 0, "trace name bandage");
    Require(std::strcmp(Name(Kind::Healing), "healing") == 0, "trace name healing");
    Require(std::strcmp(Name(Kind::Mana), "mana") == 0, "trace name mana");
    Require(std::strcmp(InventoryQuery(Kind::Healing), "healing potion") == 0, "query healing potion");
    Require(std::strcmp(InventoryQuery(Kind::Mana), "mana potion") == 0, "query mana potion");
    Require(std::strcmp(InventoryQuery(Kind::Bandage), "bandage") == 0, "query bandage");

    // UseReal 0 (default) or no roster bot on its own: legacy, the cheat path stays.
    Require(!UsesReal(false, true, Kind::Healing), "UseReal 0 keeps the legacy path");
    Require(!UsesReal(true, false, Kind::Healing), "other bots / a real master keep the legacy path");
    Require(!UsesReal(true, true, Kind::None), "food and buffs are not touched");
    Require(UsesReal(true, true, Kind::Bandage), "roster bot on its own: bandage from the bags");
    Require(UsesReal(true, true, Kind::Mana), "roster bot on its own: mana potion from the bags");

    // Threshold: 100 = trigger alone; otherwise at or below.
    Require(AtOrBelow(90.0f, 100), "100 = no extra gate");
    Require(AtOrBelow(35.0f, 35), "at the threshold");
    Require(AtOrBelow(20.0f, 35), "below the threshold");
    Require(!AtOrBelow(36.0f, 35), "above the threshold: no potion");
    Require(!AtOrBelow(1.0f, 0), "0 = never");

    // Stock: keep instead of selling, buy only with Buy, never a buy/sell loop.
    Require(Decide(false, 0.5f, 0.0f, 2, true) == Stock::Legacy, "not usable: old rules");
    Require(Decide(true, 0.5f, 1.0f, 2, true) == Stock::Legacy, "outclassed by a better stack: old rules (sold)");
    Require(Decide(true, 0.0f, 0.0f, 2, true) == Stock::Buy, "empty with Buy: buy");
    Require(Decide(true, 0.0f, 0.0f, 2, false) == Stock::Keep, "Buy 0: nothing bought");
    Require(Decide(true, 0.4f, 0.0f, 2, false) == Stock::Keep, "a self-made bandage is kept");
    Require(Decide(true, 0.4f, 0.5f, 2, true) == Stock::Buy, "own + better below one stack and Buy: buy");
    Require(Decide(true, 0.6f, 0.5f, 2, true) == Stock::Keep, "own + better at one stack: kept, not bought");
    Require(Decide(true, 1.5f, 0.0f, 2, true) == Stock::Keep, "below KeepStacks: kept");
    Require(Decide(true, 2.0f, 0.0f, 2, true) == Stock::Legacy, "at KeepStacks: old rules");
    Require(Decide(true, 2.5f, 0.0f, 3, true) == Stock::Keep, "KeepStacks 3 keeps 2.5");
    Require(KeepLimit(0) == 2.0f && KeepLimit(1) == 2.0f && KeepLimit(3) == 3.0f, "keep limit at least two stacks");
    // No loop: whatever a purchase leaves (up to one stack and a bit), it is kept, not sold.
    for (std::uint32_t keep = 0; keep <= 3; ++keep)
        for (float own = 0.0f; own <= 1.5f; own += 0.05f)
            Require(Decide(true, own, 0.0f, keep, true) != Stock::Legacy, "a bought stack is never sold again");

    // Purchase (owner decision 04.10): only vendor goods, bandages never bought.
    Require(BuyAllowed(true, Kind::Healing, true), "vendor healing potion with Buy: bought");
    Require(BuyAllowed(true, Kind::Mana, true), "vendor mana potion with Buy: bought");
    Require(!BuyAllowed(false, Kind::Healing, true), "Buy 0: nothing bought");
    Require(!BuyAllowed(true, Kind::Healing, false), "not sold by any vendor: not bought");
    Require(!BuyAllowed(true, Kind::Bandage, true), "a vendor bandage is never bought");
    Require(!BuyAllowed(true, Kind::Bandage, false), "a bandage is never bought");
    Require(!BuyAllowed(true, Kind::None, true), "no consumable kind: not bought");
    Require(Decide(true, 0.0f, 0.0f, 2, BuyAllowed(true, Kind::Bandage, true)) == Stock::Keep, "no bandage in the bags: kept, not bought");
    Require(Decide(true, 0.0f, 0.0f, 2, BuyAllowed(true, Kind::Healing, false)) == Stock::Keep, "non-vendor potion: kept, not bought");
    Require(Decide(true, 0.0f, 0.0f, 2, BuyAllowed(true, Kind::Mana, true)) == Stock::Buy, "vendor mana potion: bought");

    // Trace throttle.
    Require(TraceDue(1000, 0, 300), "first use logs");
    Require(!TraceDue(1200, 1000, 300), "inside the cooldown: counted only");
    Require(TraceDue(1300, 1000, 300), "after the cooldown: logs");

    std::cout << "consumables_policy: ok\n";
    return 0;
}
