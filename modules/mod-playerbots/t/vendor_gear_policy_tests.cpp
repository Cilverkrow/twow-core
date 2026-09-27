#include "VendorGearPolicy.h"

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

bool Reason(ai::vendor_gear::Decision const& d, char const* reason)
{
    return !std::strcmp(d.reason, reason);
}

ai::vendor_gear::Offer Upgrade(std::uint32_t newScore, std::uint32_t oldScore, std::uint32_t price)
{
    ai::vendor_gear::Offer offer;
    offer.equipUpgrade = true;
    offer.newScore = newScore;
    offer.oldScore = oldScore;
    offer.price = price;
    return offer;
}
}

int main()
{
    using namespace ai::vendor_gear;

    Settings settings;
    settings.enabled = true;

    // Scope: switch, roster bot on its own, level cap (inclusive).
    Require(!InScope(Settings(), true, 10), "default off");
    Require(InScope(settings, true, 30), "level 30 is in scope");
    Require(!InScope(settings, true, 31), "level 31 is above the cap");
    Require(!InScope(settings, false, 10), "bots with a real player master keep the old rule");
    settings.maxLevel = 20;
    Require(!InScope(settings, true, 21), "cap is configurable");
    settings.maxLevel = 30;

    // Budget: 10 gold start, 50 % per visit, repairs and training kept.
    Require(VisitAllowance(settings, 100000, 0) == 50000, "half the money");
    Require(VisitAllowance(settings, 100000, 80000) == 20000, "never below the reserve");
    Require(VisitAllowance(settings, 5000, 5000) == 0, "money equal to the reserve: nothing");
    Require(VisitAllowance(settings, 1000, 4000) == 0, "money below the reserve: nothing");
    settings.maxSpendPercent = 250;
    Require(VisitAllowance(settings, 1000, 0) == 1000, "percent is clamped to 100");
    settings.maxSpendPercent = 0;
    Require(VisitAllowance(settings, 1000, 0) == 0, "0 percent spends nothing");
    settings.maxSpendPercent = 50;
    Require(VisitAllowance(settings, 0xFFFFFFFFu, 0) == 0x7FFFFFFFu, "no overflow on large purses");

    // Upgrade / no upgrade by stat score.
    Decision d = Decide(Upgrade(120, 100, 500), 1000, 0);
    Require(d.buy && Reason(d, "score_gain"), "higher score: buy");
    d = Decide(Upgrade(100, 100, 500), 1000, 0);
    Require(!d.buy && Reason(d, "no_score_gain"), "equal score is no upgrade");
    d = Decide(Upgrade(80, 100, 500), 1000, 0);
    Require(!d.buy && Reason(d, "no_score_gain"), "lower score is no upgrade");
    Offer empty = Upgrade(40, 0, 500);
    empty.slotEmpty = true;
    d = Decide(empty, 1000, 0);
    Require(d.buy && Reason(d, "empty_slot"), "empty slot with a score: buy");
    empty.newScore = 0;
    Require(!Decide(empty, 1000, 0).buy, "empty slot, item without measurable value: skip");
    Offer notUsable = Upgrade(500, 0, 10);
    notUsable.equipUpgrade = false;
    d = Decide(notUsable, 1000, 0);
    Require(!d.buy && Reason(d, "not_upgrade"), "item usage says no (class/spec/proficiency): skip");

    // Budget per visit, cumulative.
    d = Decide(Upgrade(120, 100, 1001), 1000, 0);
    Require(!d.buy && Reason(d, "budget"), "over the allowance: skip");
    Require(Decide(Upgrade(120, 100, 1000), 1000, 0).buy, "exactly the allowance: buy");
    Require(!Decide(Upgrade(120, 100, 400), 1000, 700).buy, "what the visit spent counts");
    Require(!Decide(Upgrade(120, 100, 0), 0, 0).buy, "no allowance: nothing, even for free");

    // No repeat buy.
    Offer owned = Upgrade(120, 100, 10);
    owned.alreadyOwned = true;
    Require(Reason(Decide(owned, 1000, 0), "already_owned"), "already in the bags: skip");
    Offer before = Upgrade(120, 100, 10);
    before.boughtBefore = true;
    Require(Reason(Decide(before, 1000, 0), "bought_before"), "bought earlier (then sold): skip");
    Offer slot = Upgrade(120, 100, 10);
    slot.slotBoughtThisVisit = true;
    Require(Reason(Decide(slot, 1000, 0), "slot_done_this_visit"), "one item per slot per visit");
    Offer cool = Upgrade(120, 100, 10);
    cool.cooldownActive = true;
    Require(Reason(Decide(cool, 1000, 0), "cooldown"), "cooldown: skip");

    // Cooldown.
    settings.cooldownSeconds = 600;
    Require(!CooldownActive(settings, 5000, 0), "never bought: no cooldown");
    Require(CooldownActive(settings, 1000000 + 599999, 1000000), "within the cooldown");
    Require(!CooldownActive(settings, 1000000 + 600000, 1000000), "cooldown over");
    Require(!CooldownActive(settings, 10, 1000000), "clock went back: no cooldown lock-out");

    // Contract against buy loops (train 6: 142 axes, #333: 740 threads): the
    // buy action asks up to 10 times per item and visit, and item usage still
    // says EQUIP while the bought copy sits in the bags. Only one copy is bought,
    // the slot is closed for the visit, and later visits within the cooldown or
    // after the item was sold again do not buy it again.
    {
        Memory memory;
        memory.BeginVisit();
        std::uint32_t spent = 0;
        std::uint32_t const allowance = 5000;
        std::uint32_t const itemId = 2488, chestSlot = 4, price = 300;
        bool owned = false;
        int buys = 0;
        for (int n = 0; n < 10; ++n)
        {
            Offer offer = Upgrade(60, 20, price);
            offer.alreadyOwned = owned;
            offer.boughtBefore = memory.BoughtBefore(itemId);
            offer.slotBoughtThisVisit = memory.SlotDone(chestSlot);
            if (!Decide(offer, allowance, spent).buy)
                continue;
            ++buys;
            spent += price;
            owned = true;
            memory.Record(itemId, chestSlot, 1000);
        }
        Require(buys == 1, "one purchase per item in a visit");

        // A second chest in the same visit: slot already done.
        Offer other = Upgrade(80, 20, price);
        other.slotBoughtThisVisit = memory.SlotDone(chestSlot);
        Require(!Decide(other, allowance, spent).buy, "second item for the same slot in one visit");

        // Next visit, item sold meanwhile (not owned), cooldown over: still not again.
        memory.BeginVisit();
        Offer again = Upgrade(60, 20, price);
        again.boughtBefore = memory.BoughtBefore(itemId);
        again.slotBoughtThisVisit = memory.SlotDone(chestSlot);
        Require(!memory.SlotDone(chestSlot), "slots reset per visit");
        Require(!Decide(again, allowance, 0).buy, "never the same item twice");
    }

    // Memory and store are bounded.
    {
        Memory memory;
        for (std::uint32_t i = 0; i < Memory::MaxRemembered * 3; ++i)
            memory.Record(i + 1, 0, 1);
        Require(memory.BoughtBefore(Memory::MaxRemembered * 3), "last item remembered");
        Require(!memory.BoughtBefore(1), "memory is bounded");

        Store& store = Store::Instance();
        Memory stored;
        stored.Record(7, 1, 42);
        store.Put(1, stored);
        Require(store.Get(1).BoughtBefore(7) && store.Get(1).LastPurchaseMs() == 42, "store round trip");
        Require(!store.Get(2).BoughtBefore(7), "unknown bot: empty memory");
        for (std::uint32_t guid = 10; guid < 10 + Store::MaxBots + 5; ++guid)
            store.Put(guid, stored);
        Require(store.Size() <= Store::MaxBots, "store is bounded");
    }

    return 0;
}
