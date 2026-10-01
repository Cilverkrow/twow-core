#pragma once

#include <cstdint>
#include <iterator>
#include <map>

namespace ai::unreachable
{
// Hotfix 8.2 (#421, [Idle] of hotfix 8.1): stuck bots chased a target they never
// reached - every tick "enemy out of melee range" pushed "reach melee", which moved
// and reported OK, and nothing ever counted as a failure (the Mulgore shamans on
// Bristleback Shamans, 70 minutes). A target the bot does not get closer to for
// WindowSeconds counts as unreachable and is ignored for IgnoreSeconds.
constexpr uint32_t WindowSeconds = 20;
constexpr float MinGainYards = 1.0f;
// A pause between two reach attempts longer than this starts a new window.
constexpr uint32_t GapSeconds = 5;
constexpr uint32_t IgnoreSeconds = 300;
constexpr uint32_t LogSeconds = 300;
constexpr size_t MaxIgnored = 16;

struct Approach
{
    uint64_t target = 0;
    uint32_t start = 0;
    uint32_t last = 0;
    float best = 0.f;

    // One reach attempt at the given distance; true when the target counts as unreachable.
    bool Update(uint64_t guid, float distance, uint32_t now)
    {
        if (guid != target || !last || now - last > GapSeconds)
        {
            target = guid;
            start = now;
            best = distance;
            last = now;
            return false;
        }
        last = now;
        if (distance + MinGainYards <= best)
        {
            best = distance;
            start = now;
            return false;
        }
        return now - start >= WindowSeconds;
    }

    void Reset() { *this = Approach(); }
};

struct IgnoreList
{
    std::map<uint64_t, uint32_t> until;

    void Add(uint64_t guid, uint32_t now)
    {
        for (auto it = until.begin(); it != until.end();)
            it = it->second <= now ? until.erase(it) : std::next(it);
        if (until.size() >= MaxIgnored)
        {
            auto oldest = until.begin();
            for (auto it = until.begin(); it != until.end(); ++it)
                if (it->second < oldest->second)
                    oldest = it;
            until.erase(oldest);
        }
        until[guid] = now + IgnoreSeconds;
    }

    bool Ignored(uint64_t guid, uint32_t now) const
    {
        auto const it = until.find(guid);
        return it != until.end() && it->second > now;
    }
};
}
