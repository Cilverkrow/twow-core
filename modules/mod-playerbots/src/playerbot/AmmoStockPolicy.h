#pragma once

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace ai::ammo_stock
{
// OB-10 train 6: a warrior bought 142 Crude Throwing Axes. Thrown weapons stack
// to 200, are used up per throw like ammo and are bought one at a time, and the
// ammo rule asked for 2 full stacks (400 axes) for warriors and rogues and 8
// stacks for a hunter's arrows. The wanted stock is a COUNT of items here, so a
// cap below one stack is possible. Every cap is 0 = class default (unchanged).

// Owner decision 2026-09-27 (twow-repo#363): a hunter refills to a target stock
// by level, e.g. "10:400,30:800,50:1200" = up to L10 400 shots, up to L30 800,
// up to L50 1200; above the last tier the last count applies.
struct Tier
{
    std::uint32_t maxLevel = 0;
    std::uint32_t count = 0;
};

// "maxLevel:count" pairs separated by commas; malformed or zero entries are
// ignored, the result is sorted by level.
inline std::vector<Tier> ParseTiers(std::string const& text)
{
    std::vector<Tier> tiers;
    std::stringstream list(text);
    std::string entry;
    while (std::getline(list, entry, ','))
    {
        std::size_t const colon = entry.find(':');
        if (colon == std::string::npos)
            continue;
        try
        {
            unsigned long const level = std::stoul(entry.substr(0, colon));
            unsigned long const count = std::stoul(entry.substr(colon + 1));
            if (level && count && level <= 255 && count <= 100000)
                tiers.push_back({ std::uint32_t(level), std::uint32_t(count) });
        }
        catch (...)
        {
        }
    }
    std::sort(tiers.begin(), tiers.end(), [](Tier const& a, Tier const& b) { return a.maxLevel < b.maxLevel; });
    return tiers;
}

inline std::uint32_t TierCount(std::vector<Tier> const& tiers, std::uint32_t level)
{
    for (Tier const& tier : tiers)
        if (level <= tier.maxLevel)
            return tier.count;
    return tiers.empty() ? 0 : tiers.back().count;
}

struct Settings
{
    std::vector<Tier> hunterTiers;  // empty = class default
    bool hunterFillQuiver = false;  // with a fitting quiver/ammo pouch: fill it
    std::uint32_t thrownMaxCount = 0;  // 0 = class default
};

struct Facts
{
    bool hunter = false;
    bool thrown = false;
    bool itemCheat = false;
    std::uint32_t level = 1;
    std::uint32_t stackSize = 1;
    std::uint32_t quiverCapacity = 0;  // shots a fitting equipped quiver holds, 0 = none
};

// Number of items of this ammo the bot wants to carry (better ammo counts too).
inline std::uint32_t TargetCount(Settings const& settings, Facts const& facts)
{
    std::uint32_t const stack = facts.stackSize ? facts.stackSize : 1;
    std::uint32_t target = (facts.hunter && !facts.thrown ? 8 : 2) * stack;

    if (facts.hunter && !facts.thrown)
    {
        if (settings.hunterFillQuiver && facts.quiverCapacity)
            target = facts.quiverCapacity;
        else if (std::uint32_t const tier = TierCount(settings.hunterTiers, facts.level))
            target = tier;
    }

    if (facts.thrown && settings.thrownMaxCount)
        target = std::min(target, settings.thrownMaxCount);

    // The item cheat only needs one stack, and never more than a cap.
    if (facts.itemCheat)
        target = std::min(target, stack);

    return target;
}

// Units (vendor batches of buyCount) for one purchase: the missing amount in
// one transaction, at most one stack, at least one unit, at most 255 (the
// vendor packet field), and no more than the money pays for.
inline std::uint32_t PurchaseUnits(std::uint32_t target, std::uint32_t carried, std::uint32_t buyCount,
    std::uint32_t stackSize, std::uint32_t unitPrice, std::uint32_t money)
{
    if (carried >= target)
        return 0;
    std::uint32_t const batch = buyCount ? buyCount : 1;
    std::uint32_t const missing = target - carried;
    std::uint32_t units = (missing + batch - 1) / batch;
    std::uint32_t const perStack = std::max<std::uint32_t>(1, (stackSize ? stackSize : 1) / batch);
    units = std::min({ units, perStack, std::uint32_t(255) });
    if (unitPrice)
        units = std::min(units, money / unitPrice);
    return units;
}
}
