#pragma once

// twow-repo#587 Z4 (owner 14.10.2026: "fällt es dann nicht mehr auf, wenn sie eigentlich wo hin wollen, wo sie nicht
// können?"): giving up a turn-in must not hide an unreachable target. Every stage-1 recovery of a turn-in is logged as
// [UnreachableTarget] (always on, whatever AiPlayerbot.ZoneExit.TurnInStallCounts says), and the targets are counted
// for [UnreachableTop] lines over 5 and 60 minutes - the list CLI-PATH works from. Map threads write, the world
// thread reads and resets: one mutex, a bounded map.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace ai::unreachable_target
{
    // A target: the creature entry and its place, rounded to 10 yd (one taker = one key, whatever bot asks).
    struct Key
    {
        std::int32_t entry = 0;
        std::uint32_t map = 0;
        std::int32_t x10 = 0;
        std::int32_t y10 = 0;
        bool operator<(Key const& o) const { return std::tie(entry, map, x10, y10) < std::tie(o.entry, o.map, o.x10, o.y10); }
    };

    inline Key KeyOf(std::int32_t entry, std::uint32_t map, float x, float y)
    {
        return Key{ entry, map, std::int32_t(std::lround(x / 10.0f)), std::int32_t(std::lround(y / 10.0f)) };
    }

    struct Entry
    {
        std::uint32_t count = 0;
        std::uint32_t quest = 0;
        std::set<std::uint32_t> bots;
    };

    constexpr std::size_t MaxTargets = 512;

    class Book
    {
    public:
        void Add(Key const& key, std::uint32_t quest, std::uint32_t bot)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (entries.size() >= MaxTargets && entries.find(key) == entries.end())
                return;
            Entry& e = entries[key];
            ++e.count;
            e.quest = quest;
            if (e.bots.size() < 64)
                e.bots.insert(bot);
        }

        // The n targets with the most recoveries since the last call, and the total; the book starts empty again.
        std::vector<std::pair<Key, Entry>> TakeTop(std::size_t n, std::uint32_t& total)
        {
            std::map<Key, Entry> taken;
            {
                std::lock_guard<std::mutex> lock(mutex);
                taken.swap(entries);
            }
            std::vector<std::pair<Key, Entry>> all(taken.begin(), taken.end());
            total = 0;
            for (auto const& item : all)
                total += item.second.count;
            std::sort(all.begin(), all.end(), [](auto const& a, auto const& b) { return a.second.count > b.second.count; });
            if (all.size() > n)
                all.resize(n);
            return all;
        }

    private:
        std::mutex mutex;
        std::map<Key, Entry> entries;
    };

    inline Book& ShortWindow() { static Book book; return book; }   // [UnreachableTop] window_min=5
    inline Book& LongWindow() { static Book book; return book; }    // [UnreachableTop] window_min=60

    // "entry:map:x:y:quest:count:bots" per target, comma-separated.
    inline std::string Format(std::vector<std::pair<Key, Entry>> const& top)
    {
        std::string out;
        for (auto const& [key, e] : top)
        {
            if (!out.empty())
                out += ",";
            out += std::to_string(key.entry) + ":" + std::to_string(key.map) + ":" + std::to_string(key.x10 * 10) + ":" +
                std::to_string(key.y10 * 10) + ":" + std::to_string(e.quest) + ":" + std::to_string(e.count) + ":" +
                std::to_string(e.bots.size());
        }
        return out;
    }
}
