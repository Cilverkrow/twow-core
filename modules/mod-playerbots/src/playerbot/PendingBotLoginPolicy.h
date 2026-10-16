#pragma once

// twow-repo#595 (4) (CLI-DEEPDIVE, review of twow-core#499, 16.10.2026): `.bot add` queues the bot's character load with
// a callback bound to the master's PlayerbotHolder. When the master logs out first, RemoveBotMgr deletes that holder and
// the callback later ran on freed memory (OnBotLogin -> virtual OnBotLoginInternal). Now the callback is a static
// function and every pending login names its owner: ~PlayerbotHolder orphans its own pending logins, an orphaned login
// is dropped (its query holder freed), every other one goes to its owner as before. World thread only, like the map.

#include <cstdint>

namespace ai::pending_login
{
    enum class Route
    {
        Unknown,    // not a pending .bot add login: nothing to do (as before)
        Orphaned,   // its holder is gone: drop the login, free the query holder
        Deliver,    // hand it to the owner's HandlePlayerBotLoginCallback
    };

    template <class Map, class Key>
    Route RouteOf(Map const& pending, Key const& key)
    {
        auto const it = pending.find(key);
        if (it == pending.end())
            return Route::Unknown;
        return it->second.owner ? Route::Deliver : Route::Orphaned;
    }

    // The holder `owner` is being destroyed: its pending logins lose their owner. Returns how many.
    template <class Map, class Owner>
    std::uint32_t Orphan(Map& pending, Owner const* owner)
    {
        std::uint32_t orphaned = 0;
        for (auto& entry : pending)
        {
            if (entry.second.owner && entry.second.owner == owner)
            {
                entry.second.owner = nullptr;
                ++orphaned;
            }
        }
        return orphaned;
    }
}
