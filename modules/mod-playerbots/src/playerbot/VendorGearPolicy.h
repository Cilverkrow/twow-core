#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace ai::vendor_gear
{
// twow-repo#363: up to a level cap, a roster bot on its own buys gear from a
// vendor it visits anyway, but only a measurable upgrade by the stat score,
// within a budget that keeps money for repairs and class training, at most one
// item per equipment slot per visit, never the same item twice, and not again
// before a cooldown has passed. Every rule is a pure function here so the
// policy is testable without a world.
struct Settings
{
    bool enabled = false;
    std::uint32_t maxLevel = 30;
    std::uint32_t maxSpendPercent = 50;   // of the money carried at the start of the visit
    std::uint32_t reserveCopper = 0;      // on top of repair and class training costs
    std::uint32_t cooldownSeconds = 600;  // between two visits that bought gear
};

// Money a visit may spend on gear: never below the reserve, and at most
// maxSpendPercent of what the bot carried when the visit began.
inline std::uint32_t VisitAllowance(Settings const& settings, std::uint32_t money, std::uint32_t reserve)
{
    if (money <= reserve)
        return 0;
    std::uint64_t const aboveReserve = money - reserve;
    std::uint64_t percent = settings.maxSpendPercent > 100 ? 100 : settings.maxSpendPercent;
    std::uint64_t const share = std::uint64_t(money) * percent / 100;
    return std::uint32_t(share < aboveReserve ? share : aboveReserve);
}

inline bool InScope(Settings const& settings, bool rosterOnItsOwn, std::uint32_t botLevel)
{
    return settings.enabled && rosterOnItsOwn && botLevel <= settings.maxLevel;
}

inline bool CooldownActive(Settings const& settings, std::uint64_t nowMs, std::uint64_t lastPurchaseMs)
{
    if (!lastPurchaseMs || nowMs < lastPurchaseMs)
        return false;
    return nowMs - lastPurchaseMs < std::uint64_t(settings.cooldownSeconds) * 1000;
}

struct Offer
{
    bool equipUpgrade = false;       // item usage says EQUIP (class, spec, armour and weapon proficiency)
    bool slotEmpty = false;
    std::uint32_t newScore = 0;      // stat score of the offered item
    std::uint32_t oldScore = 0;      // stat score of the item in that slot
    std::uint32_t price = 0;
    bool alreadyOwned = false;       // in the bags or equipped
    bool boughtBefore = false;       // bought by this policy earlier in this session
    bool slotBoughtThisVisit = false;
    bool cooldownActive = false;
};

struct Decision
{
    bool buy = false;
    char const* reason = "";
};

// Order matters only for the reason that is reported; every rule must pass.
inline Decision Decide(Offer const& offer, std::uint32_t allowance, std::uint32_t spentThisVisit)
{
    if (offer.cooldownActive)
        return { false, "cooldown" };
    if (!offer.equipUpgrade)
        return { false, "not_upgrade" };
    if (offer.alreadyOwned)
        return { false, "already_owned" };
    if (offer.boughtBefore)
        return { false, "bought_before" };
    if (offer.slotBoughtThisVisit)
        return { false, "slot_done_this_visit" };
    if (offer.slotEmpty ? offer.newScore == 0 : offer.newScore <= offer.oldScore)
        return { false, "no_score_gain" };
    if (spentThisVisit >= allowance || offer.price > allowance - spentThisVisit)
        return { false, "budget" };
    return { true, offer.slotEmpty ? "empty_slot" : "score_gain" };
}

// Per-bot memory: item ids bought (bounded, so a bot that lives for weeks does
// not grow it without limit), the slots bought in the current visit and the
// time of the last purchase.
class Memory
{
public:
    static constexpr std::size_t MaxRemembered = 64;

    bool BoughtBefore(std::uint32_t itemId) const { return m_bought.count(itemId) != 0; }
    bool SlotDone(std::uint32_t slot) const { return m_visitSlots.count(slot) != 0; }
    std::uint64_t LastPurchaseMs() const { return m_lastPurchaseMs; }

    void BeginVisit() { m_visitSlots.clear(); }

    void Record(std::uint32_t itemId, std::uint32_t slot, std::uint64_t nowMs)
    {
        if (m_bought.size() >= MaxRemembered)
            m_bought.clear();
        m_bought.insert(itemId);
        m_visitSlots.insert(slot);
        m_lastPurchaseMs = nowMs;
    }

private:
    std::unordered_set<std::uint32_t> m_bought;
    std::unordered_set<std::uint32_t> m_visitSlots;
    std::uint64_t m_lastPurchaseMs = 0;
};

// One locked map for all bots, read by the buy action and by the "vendor has
// useful item" value. No AI context values per item (see #351: per-entry
// values were never removed and slowed every tick). Bounded: cleared when full.
class Store
{
public:
    static constexpr std::size_t MaxBots = 8192;

    static Store& Instance()
    {
        static Store store;
        return store;
    }

    Memory Get(std::uint32_t botGuid) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const it = m_bots.find(botGuid);
        return it == m_bots.end() ? Memory() : it->second;
    }

    void Put(std::uint32_t botGuid, Memory const& memory)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_bots.size() >= MaxBots && !m_bots.count(botGuid))
            m_bots.clear();
        m_bots[botGuid] = memory;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_bots.size();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint32_t, Memory> m_bots;
};
}
