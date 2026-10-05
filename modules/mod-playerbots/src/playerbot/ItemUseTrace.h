#pragma once

#include <cstdint>

namespace ai::item_use
{
// Hotfix 8.9 (twow-repo#474): items used through the player's handler instead of a queued
// packet that was never processed. One [ItemUse] line per bot: the first after 20 events,
// so a test sees it quickly, then at most one per hour.
struct Trace
{
    uint32_t uses = 0;        // ImbueItem calls (bandages, health items, poisons, oils)
    uint32_t started = 0;     // a cast was running right after the use
    uint32_t bandages = 0;    // of them bandages
    uint32_t opens = 0;       // items with loot opened (quest containers)
    uint32_t start = 0;
    bool logged = false;

    void OnUse(bool castStarted, bool bandage, uint32_t now)
    {
        Begin(now);
        ++uses;
        if (castStarted)
            ++started;
        if (bandage)
            ++bandages;
    }

    void OnOpen(uint32_t now)
    {
        Begin(now);
        ++opens;
    }

    bool Due(uint32_t now) const
    {
        uint32_t const events = uses + opens;
        return (!logged && events >= 20) || (events && now - start >= 3600);
    }

    void Reset()
    {
        uses = started = bandages = opens = start = 0;
        logged = true;
    }

private:
    void Begin(uint32_t now)
    {
        if (!uses && !opens)
            start = now;
    }
};
}
