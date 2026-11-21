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

// ---- AiPlayerbot.Rotation.UnreachableLock (twow-repo#703 item 4b, design 20.11.2026 L1-L8; cohort mode 0 off (default) / 1 all /
// 2 cohort B / 3 cohort A, other values -> 0; only the give-up above arms it).
//
// 703H u4: 8 [StuckCombat] stops followed a same-bot [UnreachableGiveUp] (1.77 by chance), 5 on the given-up entry: the bot
// picked the given-up guid or a sibling of the same camp again. After a give-up a bot on its own (no group) locks that guid
// anywhere, and the same entry on the same map within EntryRadiusYards of the give-up spot, for LockSeconds on its own clock
// (an IgnoreList overwrite cannot shorten it). A locked unit is still picked in self-defence (MayDefend).

constexpr std::uint32_t LockSeconds = LongIgnoreSeconds;   // 1800, the span of the give-up ignore
constexpr float EntryRadiusYards = 100.0f;                 // a same-entry sibling counts within this 2D radius of the spot
constexpr std::size_t LockSlots = 4;                       // locks per bot
constexpr std::uint32_t LockLogSeconds = 60;               // at most one state=block line per bot per span

// The pick paths that read the lock (the path= and n_* fields of the block line).
enum class Path : std::uint8_t { GrindAttacker, GrindCandidate, Assist, Attack, Pull, Count };
enum class Match : std::uint8_t { None, Guid, Entry };

inline char const* PathName(Path path)
{
    switch (path)
    {
        case Path::GrindAttacker: return "grind_att";
        case Path::GrindCandidate: return "grind_cand";
        case Path::Assist: return "assist";
        case Path::Attack: return "attack";
        case Path::Pull: return "pull";
        default: return "none";
    }
}

inline char const* MatchName(Match match)
{
    return match == Match::Guid ? "guid" : match == Match::Entry ? "entry" : "none";
}

// The arm gate: mode 0 = no lock for any bot (the caller passes cohort::Applies(mode, guid)); a bot in a group never (L3).
inline bool LockActive(std::uint32_t mode, bool cohortApplies, bool inGroup)
{
    return mode != 0 && cohortApplies && !inGroup;
}

// Self-defence per match type (L2): a sibling (Entry) when its victim is the bot or its pet; the given-up guid only when it
// attacks the bot or its pet AND the bot can hit it now (reach to it stays blocked). The caller computes hittableNow lazily.
inline bool MayDefend(Match match, bool attacksBotOrPet, bool hittableNow)
{
    if (match == Match::Entry)
        return attacksBotOrPet;
    if (match == Match::Guid)
        return attacksBotOrPet && hittableNow;
    return true;
}

struct Lock
{
    std::uint64_t guid = 0;     // the given-up unit (0 = no guid match)
    std::int32_t entry = 0;     // its entry (0 = no sibling match)
    std::uint32_t map = 0;
    float x = 0.0f;             // the give-up spot
    float y = 0.0f;
    std::uint32_t until = 0;    // seconds; live while until > now (0 = free)
};

// The per-bot locks (the bot's own thread).
struct Locks
{
    Lock slots[LockSlots];
    std::uint32_t latest = 0;   // the latest until (the fast path)

    // Locks guid, and its entry near (map, x, y), until now + LockSeconds: its own slot again, else a free or run-out one, else
    // the one that runs out first. Returns true when that pushed out a live lock.
    bool Arm(std::uint64_t guid, std::int32_t entry, std::uint32_t map, float x, float y, std::uint32_t now)
    {
        Lock* slot = nullptr;
        for (Lock& l : slots)
            if (guid && l.guid == guid)
                slot = &l;
        for (Lock& l : slots)
            if (!slot && l.until <= now)
                slot = &l;
        bool evicted = false;
        if (!slot)
        {
            slot = &slots[0];
            for (Lock& l : slots)
                if (l.until < slot->until)
                    slot = &l;
            evicted = true;
        }
        *slot = Lock{ guid, entry, map, x, y, now + LockSeconds };
        if (slot->until > latest)
            latest = slot->until;
        return evicted;
    }

    bool Any(std::uint32_t now) const { return latest > now; }

    // Guid: a live lock of this guid, anywhere (guid 0 never). Entry: a live lock of this entry (0 never) on the same map within
    // EntryRadiusYards (2D) of its spot. Guid wins over Entry.
    Match Find(std::uint64_t guid, std::int32_t entry, std::uint32_t map, float x, float y, std::uint32_t now) const
    {
        Match match = Match::None;
        for (Lock const& l : slots)
        {
            if (l.until <= now)
                continue;
            if (guid && l.guid == guid)
                return Match::Guid;
            if (entry && l.entry == entry && l.map == map && Near(l, x, y))
                match = Match::Entry;
        }
        return match;
    }

    // The seconds left of the longest live lock of that match type (0 = none).
    std::uint32_t LeftFor(Match match, std::uint64_t guid, std::int32_t entry, std::uint32_t map, float x, float y,
        std::uint32_t now) const
    {
        std::uint32_t left = 0;
        for (Lock const& l : slots)
        {
            if (l.until <= now)
                continue;
            bool const hit = match == Match::Guid ? (guid && l.guid == guid) :
                (match == Match::Entry && entry && l.entry == entry && l.map == map && Near(l, x, y));
            if (hit && l.until - now > left)
                left = l.until - now;
        }
        return left;
    }

    static bool Near(Lock const& l, float x, float y)
    {
        float const dx = l.x - x;
        float const dy = l.y - y;
        return dx * dx + dy * dy <= EntryRadiusYards * EntryRadiusYards;
    }
};

// The state=block line budget (L6 visibility): blocks per path and defended verdicts since the last line. A line is due for a
// block (never for a defended verdict), the first at once, then at most one per LockLogSeconds; the caller then calls Reset.
// The n_* counts are read calls, not units (FindTarget asks once per target value and tick), and blocks after the last line
// are not flushed when no later block comes: they overstate the units and miss the tail.
struct BlockLog
{
    std::uint32_t blocked[std::size_t(Path::Count)] = {};
    std::uint32_t defended = 0;
    std::uint32_t last = 0;
    bool logged = false;

    bool Note(Path path, bool defend, std::uint32_t now)
    {
        if (defend)
        {
            ++defended;
            return false;
        }
        if (path < Path::Count)
            ++blocked[std::size_t(path)];
        return !logged || now - last >= LockLogSeconds;
    }

    void Reset(std::uint32_t now)
    {
        for (std::uint32_t& n : blocked)
            n = 0;
        defended = 0;
        last = now;
        logged = true;
    }

    std::uint32_t Blocked(Path path) const { return path < Path::Count ? blocked[std::size_t(path)] : 0; }
};
}
