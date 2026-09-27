#include "AmmoStockPolicy.h"

#include <cstdlib>
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

ai::ammo_stock::Facts Hunter(std::uint32_t level, std::uint32_t quiver = 0)
{
    ai::ammo_stock::Facts facts;
    facts.hunter = true;
    facts.level = level;
    facts.stackSize = 200;
    facts.quiverCapacity = quiver;
    return facts;
}

ai::ammo_stock::Facts Thrower()
{
    ai::ammo_stock::Facts facts;
    facts.thrown = true;
    facts.level = 12;
    facts.stackSize = 200;
    return facts;
}
}

int main()
{
    using namespace ai::ammo_stock;

    // Neutral defaults: class default unchanged.
    Settings neutral;
    Require(TargetCount(neutral, Hunter(5)) == 1600, "hunter default: 8 stacks");
    Require(TargetCount(neutral, Hunter(40, 3200)) == 1600, "quiver ignored unless enabled");
    Require(TargetCount(neutral, Thrower()) == 400, "thrown default: 2 stacks");

    // Tier parsing.
    std::vector<Tier> tiers = ParseTiers("30:800, 10:400,50:1200");
    Require(tiers.size() == 3 && tiers[0].maxLevel == 10 && tiers[2].count == 1200, "parsed and sorted");
    Require(ParseTiers("").empty() && ParseTiers("0").empty(), "empty / placeholder: no tiers");
    Require(ParseTiers("x:1,10:,:5,0:400,10:0,20:300").size() == 1, "malformed entries ignored");
    Require(TierCount({}, 10) == 0, "no tiers: 0");

    // Owner decision 2026-09-27: L1-10 400, L11-30 800, L31-50 1200, above: 1200,
    // with a fitting quiver: fill it.
    Settings owner;
    owner.hunterTiers = ParseTiers("10:400,30:800,50:1200");
    owner.hunterFillQuiver = true;
    Require(TargetCount(owner, Hunter(1)) == 400, "L1: 400");
    Require(TargetCount(owner, Hunter(10)) == 400, "L10: 400");
    Require(TargetCount(owner, Hunter(11)) == 800, "L11: 800");
    Require(TargetCount(owner, Hunter(30)) == 800, "L30: 800");
    Require(TargetCount(owner, Hunter(31)) == 1200, "L31: 1200");
    Require(TargetCount(owner, Hunter(50)) == 1200, "L50: 1200");
    Require(TargetCount(owner, Hunter(60)) == 1200, "L60 without quiver: last tier");
    Require(TargetCount(owner, Hunter(25, 2800)) == 2800, "with a quiver: fill it (14 slots x 200)");
    owner.hunterFillQuiver = false;
    Require(TargetCount(owner, Hunter(25, 2800)) == 800, "quiver fill off: tier");

    // Thrown by count (OB-10: 142 axes one by one).
    Settings thrown;
    thrown.thrownMaxCount = 20;
    Require(TargetCount(thrown, Thrower()) == 20, "thrown capped at 20 items");
    thrown.thrownMaxCount = 1000;
    Require(TargetCount(thrown, Thrower()) == 400, "a cap never raises the default");
    thrown.thrownMaxCount = 20;
    Require(TargetCount(thrown, Hunter(20)) == 1600, "thrown cap does not touch hunter ammo");

    // Item cheat: one stack at most, and still the thrown cap (OB-10 note 3).
    Facts cheat = Thrower();
    cheat.itemCheat = true;
    Require(TargetCount(thrown, cheat) == 20, "cheat keeps the thrown cap");
    Require(TargetCount(neutral, cheat) == 200, "cheat without cap: one stack");

    // Purchase units: the missing amount in one transaction, at most one stack.
    Require(PurchaseUnits(20, 3, 1, 200, 10, 100000) == 17, "thrown: 17 missing, one call");
    Require(PurchaseUnits(20, 20, 1, 200, 10, 100000) == 0, "enough: nothing");
    Require(PurchaseUnits(20, 25, 1, 200, 10, 100000) == 0, "more than enough: nothing");
    Require(PurchaseUnits(800, 0, 200, 200, 50, 100000) == 1, "arrows: one stack per transaction");
    Require(PurchaseUnits(400, 399, 200, 200, 50, 100000) == 1, "partial batch rounds up to one unit");
    Require(PurchaseUnits(1000, 0, 1, 1000, 1, 100000) == 255, "vendor field limit 255");
    Require(PurchaseUnits(20, 0, 1, 200, 10, 55) == 5, "limited by money");
    Require(PurchaseUnits(20, 0, 1, 200, 10, 5) == 0, "no money: nothing");

    // Buy-loop contract: the old 142 one-by-one purchases become one call.
    {
        std::uint32_t carried = 0, calls = 0;
        std::uint32_t const target = TargetCount(thrown, Thrower());
        for (int n = 0; n < 10; ++n)
        {
            std::uint32_t const units = PurchaseUnits(target, carried, 1, 200, 10, 100000);
            if (!units)
                break;
            carried += units;
            ++calls;
        }
        Require(carried == 20 && calls == 1, "thrown refill: one purchase call, 20 items");
    }
    return 0;
}
