#pragma once

#include <cstddef>
#include <cstdint>

#include "QuestLoopPolicy.h"

namespace ai::unreachable_giveup
{
// AiPlayerbot.Rotation.UnreachableGiveUp (twow-repo#703 item 4, CLI-PATH design v2 85574eca, OB-10 review f9ca0344 + build OK
// 11.11.2026; cohort mode 0 off (default) / 1 all / 2 cohort B / 3 cohort A, other values -> 0).
//
// Endless fight on an unreachable target: hotfix 8.2 ignores a target the bot comes no nearer to for 300 s, then the bot picks
// the same mob again (ARB N1 bot 798: 12x [Unreachable] target_entry=1501, about 260 min in combat; [StuckCombat] stop did not
// free it, only the rescue teleport did). Unstuck.CombatFightClock restarts on every target change, and DropUnreachableEngagement
// acts only for a real player's bot.
//
// With the key a roster bot without a real player master, with no group or not its leader, counts the [Unreachable] results per
// target guid (a ring of RingSlots guids, Window seconds from the first). At K on one guid it gives that target up: the whole
// engagement goes (attack / pull / old target, selection, swing), the ignore lasts LongIgnoreSeconds, and when the entry is the
// current quest objective's, the camp (quest, entry) is marked empty for MarkSeconds - at most once per MarkGuardSeconds per
// (quest, entry) - and the travel target ends, so the router picks another. Pure rules here, no game types, no config reads.

constexpr std::uint32_t K = 3;                         // [Unreachable] results on one guid ...
constexpr std::uint32_t WindowSeconds = 1800;          // ... within this span from the first
constexpr std::uint32_t LongIgnoreSeconds = 1800;      // the ignore of a given-up target (hotfix 8.2: 300 s before K)
constexpr std::uint32_t MarkSeconds = 1800;            // the empty-camp mark of the quest objective, a fixed duration
constexpr std::uint32_t MarkGuardSeconds = 3600;       // no second mark of the same (quest, entry) within this span
constexpr std::size_t RingSlots = 4;                   // guids counted per bot; a new guid takes the oldest slot
constexpr std::size_t MarkGuardSlots = 8;              // (quest, entry) marks remembered per bot for the guard

// The bot counts at all: a roster bot without a real player master, with no group or not its leader (a leading tank keeps its
// pull, OB-10 note D). The mode and the cohort are checked by the caller before this.
inline bool Counts(bool rosterBot, bool realPlayerMaster, bool inGroup, bool groupLeader)
{
    return rosterBot && !realPlayerMaster && (!inGroup || !groupLeader);
}

// The mode gate in one place: 0 = no counting for any bot (the caller passes cohort::Applies(mode, guid) as cohortApplies).
inline bool Active(std::uint32_t mode, bool cohortApplies, bool counts)
{
    return mode != 0 && cohortApplies && counts;
}

struct Slot
{
    std::uint64_t guid = 0;     // 0 = free
    std::uint32_t count = 0;    // [Unreachable] results since first
    std::uint32_t first = 0;    // the first result of this count (seconds)
};

// The per-guid ring (the bot's own thread). Two mobs alternating A, B, A, B, A reach K on A.
struct Ring
{
    Slot slots[RingSlots];

    // Counts one [Unreachable] result on guid at now and returns the count within the window. At K or more the slot is
    // cleared (the caller gives the target up); a result after the window starts the count again at 1.
    std::uint32_t Note(std::uint64_t guid, std::uint32_t now)
    {
        Slot* slot = nullptr;
        for (Slot& s : slots)
            if (s.guid == guid && guid)
                slot = &s;
        if (!slot)
        {
            // A free slot, else the one whose count began first (the oldest).
            for (Slot& s : slots)
                if (!s.guid)
                {
                    slot = &s;
                    break;
                }
            if (!slot)
            {
                slot = &slots[0];
                for (Slot& s : slots)
                    if (s.first < slot->first)
                        slot = &s;
            }
            *slot = Slot{ guid, 0, now };
        }
        if (slot->count && now - slot->first >= WindowSeconds)
            slot->count = 0;
        if (!slot->count)
            slot->first = now;
        std::uint32_t const count = ++slot->count;
        if (count >= K)
            *slot = Slot{};
        return count;
    }

    std::uint32_t CountOf(std::uint64_t guid) const
    {
        for (Slot const& s : slots)
            if (s.guid == guid && guid)
                return s.count;
        return 0;
    }
};

// The 1 h guard per (quest, entry) (OB-10 note C): a camp marked by this rule is not marked again within MarkGuardSeconds.
struct MarkGuard
{
    struct Entry
    {
        std::uint32_t quest = 0;    // 0 = free
        std::int32_t entry = 0;
        std::uint32_t at = 0;       // seconds
    };
    Entry entries[MarkGuardSlots];

    bool Allows(std::uint32_t quest, std::int32_t entry, std::uint32_t now) const
    {
        for (Entry const& e : entries)
            if (e.quest && e.quest == quest && e.entry == entry && now - e.at < MarkGuardSeconds)
                return false;
        return true;
    }

    // Its own entry again, else a free or run-out one, else the oldest.
    void Note(std::uint32_t quest, std::int32_t entry, std::uint32_t now)
    {
        Entry* slot = nullptr;
        for (Entry& e : entries)
            if (e.quest == quest && e.entry == entry)
                slot = &e;
        for (Entry& e : entries)
            if (!slot && (!e.quest || now - e.at >= MarkGuardSeconds))
                slot = &e;
        if (!slot)
        {
            slot = &entries[0];
            for (Entry& e : entries)
                if (e.at < slot->at)
                    slot = &e;
        }
        *slot = Entry{ quest, entry, now };
    }
};

// Whether quest_loop::MarkEmptyCamp(camps, quest, entry, nowMs, ...) pushes out another camp's live mark (OB-10 note B, the
// evicted= field): the same slot choice as MarkEmptyCamp - its own slot or a free / run-out one evicts nothing.
inline bool MarkEvictsLive(quest_loop::EmptyCamps const& camps, std::uint32_t quest, std::int32_t entry, std::uint32_t nowMs)
{
    for (quest_loop::EmptyCamp const& camp : camps.slots)
        if (camp.quest == quest && camp.entry == entry)
            return false;
    for (quest_loop::EmptyCamp const& camp : camps.slots)
        if (!quest_loop::CampMarkLive(camp, nowMs))
            return false;
    return true;
}
}
