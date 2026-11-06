#pragma once

// twow-repo#333 Berufe M2 shadow counter (OB-00 06.11.2026, cheat-off analysis §d; log only, no behaviour): how often the
// item cheat (RndBotCheats "item") stood in for an item - per bot, summed over kFlushSeconds and written as one
// "[CheatItem]" line by PlayerbotAI behind AiPlayerbot.CheatItem.Trace (default 0). The sums are the upper bound of what a
// bot would miss with the cheat off. Pure rules here, tested in t/cheat_item_policy_tests.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace ai
{
namespace cheat_item
{
enum class Site : std::uint8_t
{
    Use = 0,      // UseItemAction: an item spell cast with no such item in the bags
    UseHad,       // the same cast while the bags held the item (the cheat kept it)
    CastItem,     // CastItemTargetAction: an item spell cast with no such item in the bags
    CastItemHad,  // the same cast while the bags held the item
    Food,         // EatAction: the cheat food spell 24005
    Drink,        // DrinkAction: the cheat drink spell 24355
    Poison,       // a rogue weapon poison applied by the cheat (no poison item consumed)
    AmmoRefill,   // the ammo top-up in PlayerbotAI::UpdateAI: units added to the stack
    AmmoInit,     // InitAmmo from an empty ammo slot: units of the ammo in the bags afterwards
    Refresh,      // PlayerbotFactory::Refresh (ammo, food, potions, reagents, consumables): one per call
    // CLI-ROT oil loop (CHAR-CHEAT-ITEM-COOLDOWN-20261106, OB-00 06.11.: counter only, the fix comes separately): the cheat
    // path of UseItemAction / CastItemTargetAction sets the item cooldown itself right after ForceSpellStart.
    CheatCdSet,   // every pass through that cooldown branch
    SelfBlocked,  // ... while the spell was still preparing (an instant is only queued): the cast then fails on that cooldown
    Count
};

constexpr std::size_t kSites = static_cast<std::size_t>(Site::Count);
constexpr std::uint64_t kFlushSeconds = 600;

inline char const* SiteName(Site site)
{
    switch (site)
    {
        case Site::Use: return "use";
        case Site::UseHad: return "use_had";
        case Site::CastItem: return "cast_item";
        case Site::CastItemHad: return "cast_item_had";
        case Site::Food: return "food";
        case Site::Drink: return "drink";
        case Site::Poison: return "poison";
        case Site::AmmoRefill: return "ammo_refill";
        case Site::AmmoInit: return "ammo_init";
        case Site::Refresh: return "refresh";
        case Site::CheatCdSet: return "cheat_cd_set";
        case Site::SelfBlocked: return "self_blocked";
        default: return "unknown";
    }
}

// An item cast: "missing" unless the bags held at least one of the item.
inline Site UseSite(std::uint32_t inBags)
{
    return inBags ? Site::UseHad : Site::Use;
}

inline Site CastItemSite(std::uint32_t inBags)
{
    return inBags ? Site::CastItemHad : Site::CastItem;
}

// The ammo top-up sets the stack to its maximum: the units the cheat added.
inline std::uint32_t RefillAmount(std::uint32_t before, std::uint32_t maxStack)
{
    return maxStack > before ? maxStack - before : 0;
}

struct Counts
{
    std::array<std::uint32_t, kSites> n{};

    void Add(Site site, std::uint32_t amount)
    {
        std::size_t const i = static_cast<std::size_t>(site);
        if (i >= kSites)
            return;
        std::uint32_t const room = std::numeric_limits<std::uint32_t>::max() - n[i];
        n[i] += amount > room ? room : amount;
    }

    bool Any() const
    {
        for (std::uint32_t v : n)
            if (v)
                return true;
        return false;
    }

    void Reset() { n.fill(0); }
};

// One line per bot and window; the first call only starts the window.
inline bool ShouldFlush(std::uint64_t now, std::uint64_t windowStart)
{
    return windowStart != 0 && now >= windowStart + kFlushSeconds;
}

// All sites in fixed order, so every line has the same columns: "use=0 use_had=0 ... self_blocked=0".
inline std::string Format(Counts const& counts)
{
    std::string out;
    for (std::size_t i = 0; i < kSites; ++i)
    {
        if (i)
            out += ' ';
        out += SiteName(static_cast<Site>(i));
        out += '=';
        out += std::to_string(counts.n[i]);
    }
    return out;
}
}  // namespace cheat_item
}  // namespace ai
