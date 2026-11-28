#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

// twow-repo#627 GRP2030 dose lever 2 (AiPlayerbot.QuestGroups.Manager.DeadQuestEnds, default 0 = off; OB-00 (A) 27.11.2026):
// a quest whose manager QUEST groups end DeadQuestEnds times in a row with no_progress and without any objective progress of
// any member is "dead" for DeadQuestHours: the manager makes no QUEST candidate for it and the bots of its cohort wish another
// quest (ManagerFacts.cpp ReadWish skips it). T30 v1.1: 66 % of all manager QUEST groups ran on quest 255 (elite, never done,
// 15 min each). A group with progress or a turn-in on the quest resets its count. Pure (world thread of the manager only).
namespace ai::dead_quest
{
constexpr std::uint32_t kHoursDefault = 6;
constexpr std::uint32_t kHoursMax = 48;

// The hours key: 0 or above the maximum = the default / the maximum (the load writes one error line for a clamp).
inline std::uint32_t ClampHours(std::int32_t hours)
{
    if (hours <= 0)
        return kHoursDefault;
    return std::uint32_t(hours) > kHoursMax ? kHoursMax : std::uint32_t(hours);
}

enum class Note : std::uint8_t
{
    Off = 0,      // the lever is off (endsNeeded 0): nothing is kept
    Counted,      // a dead end was counted, the quest is not dead yet
    NowDead,      // this end made the quest dead (one line)
    Reset,        // progress / a turn-in: the count starts over
    AlreadyDead,  // the quest was dead already (a group formed before it died)
    Ignored,      // another end (too small, rendezvous timeout, switch off, ...): the count stays
};

class Store
{
public:
    // One ended manager QUEST group of questId. deadEnd = it ended with no_progress and no member ever made progress;
    // progress = a member made progress or turned the quest in (any other end changes nothing).
    Note End(std::uint32_t questId, bool deadEnd, bool progress, std::uint32_t nowSec, std::uint32_t endsNeeded,
        std::uint32_t hours)
    {
        if (!endsNeeded || !questId)
            return Note::Off;
        Entry& e = entries_[questId];
        if (e.deadUntil > nowSec)
            return Note::AlreadyDead;
        if (progress)
        {
            e.count = 0;
            return Note::Reset;
        }
        if (!deadEnd)
            return Note::Ignored;
        if (++e.count < endsNeeded)
            return Note::Counted;
        e.count = 0;
        e.deadUntil = nowSec + hours * 3600u;
        return Note::NowDead;
    }

    bool Dead(std::uint32_t questId, std::uint32_t nowSec) const
    {
        auto const it = entries_.find(questId);
        return it != entries_.end() && it->second.deadUntil > nowSec;
    }

    std::uint32_t Count(std::uint32_t questId) const
    {
        auto const it = entries_.find(questId);
        return it == entries_.end() ? 0u : it->second.count;
    }

    // The quests dead at nowSec (for the bots' view); expired entries without a count are dropped.
    void DeadList(std::uint32_t nowSec, std::vector<std::uint32_t>& out)
    {
        out.clear();
        for (auto it = entries_.begin(); it != entries_.end();)
        {
            if (it->second.deadUntil > nowSec)
                out.push_back(it->first);
            else if (!it->second.count)
            {
                it = entries_.erase(it);
                continue;
            }
            ++it;
        }
    }

    void Clear() { entries_.clear(); }

private:
    struct Entry
    {
        std::uint32_t count = 0;
        std::uint32_t deadUntil = 0;
    };

    std::unordered_map<std::uint32_t, Entry> entries_;
};
}
