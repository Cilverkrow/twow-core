#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai::rescue_valley
{
// twow-repo#575 R2 rescue start-valley fix (AiPlayerbot.QuestRescue.StartValleyFix, default 0; OB-10 build OK 22.11.2026 via OB-00,
// scope answers 1-4; finding dauerb-silverpine-20261119/idle-20261122/TRACE-565-2293.txt 4d1371a0). Pure rules, no game types, no
// config reads, no logging.
//
// Why: a roster bot up to StartAreaMaxLevel (10) whose rescue found no HomeHubs target fell through to the legacy start-valley branch
// of QuestRescueTargets. That branch adds the start points of the OTHER races of its faction, with no distance filter and no dedupe,
// and the pick is a plain random draw. DAUER-B (pin 2v): Human 565 L8 was rescued 51 times to Coldridge (dwarf = gnome start point,
// the same spot every time, the bot standing there already); the undead / night-elf next_zone rows that end as fallback (targets=0)
// fall into the same branch. The goblin branch pushed Razor Hill / Sparkwater without a distance check (Goblin 2293: 54 rescues).
//
// With the key, for a roster bot (QuestRescueTargets' startZoneRules):
//   (a) the start-valley branch never offers another race's start valley; the only candidate is the bot's own race's start point;
//   (b) the candidates are deduplicated (points within DedupeYards are one) and the last rescue target is excluded while the bot has
//       made no progress since that rescue (no level, no XP, no gathering skill, no quest taken / finished / handed in / dropped);
//   (c) a candidate within RescueMinDistance of the bot is dropped, as in the anchor and inn branches; the goblin Durotar pushes get
//       the same distance filter (distance only, the route is unchanged).
// An empty result is a no_target (honest stop) with its reason line; the 2 h per-bot cooldown applies as before.
// Key 0 or a non-roster bot: the legacy branch, unchanged.

constexpr float DedupeYards = 50.0f;   // two candidate points closer than this are the same rescue target (dwarf / gnome: 0 yd)

struct Point
{
    std::uint32_t map;
    float x;
    float y;
};

inline float Dist2dSq(Point const& a, Point const& b)
{
    float const dx = a.x - b.x;
    float const dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline bool SamePoint(Point const& a, Point const& b)
{
    return a.map == b.map && Dist2dSq(a, b) <= DedupeYards * DedupeYards;
}

// (a): with the fix a bot takes only its own race's start valley.
inline bool TakesValleyOf(bool fix, std::uint32_t race, std::uint32_t botRace)
{
    return !fix || race == botRace;
}

// Why a start-valley candidate was dropped (the reason field of the [QuestRescue] state=start_valley line).
enum class Drop : std::uint8_t
{
    Kept = 0,
    Near,        // within the minimum distance of the bot (it stands there already)
    Duplicate,   // the same point as a candidate kept before it
    Last         // the last rescue target, no progress since
};

inline char const* DropName(Drop d)
{
    switch (d)
    {
        case Drop::Kept: return "kept";
        case Drop::Near: return "near";
        case Drop::Duplicate: return "duplicate";
        case Drop::Last: return "last";
    }
    return "?";
}

// (b) + (c) for one candidate against the bot, the candidates kept so far and the last target. Distance is checked on the same map
// only (another map is never near); botDistance is the bot's real 3d distance to the candidate as the caller measures it.
inline Drop Check(Point const& candidate, float botDistance, float minDistance, std::vector<Point> const& kept, bool hasLast,
    Point const& last)
{
    if (botDistance <= minDistance)
        return Drop::Near;
    for (Point const& k : kept)
        if (SamePoint(candidate, k))
            return Drop::Duplicate;
    if (hasLast && SamePoint(candidate, last))
        return Drop::Last;
    return Drop::Kept;
}

// (b) progress since the last rescue (OB-10 22.11.2026: level, XP, gathering skill or quest log). The parts of the bot's progress
// check: level and XP as in the progress snapshot, the gathering term (skill value under a declared purpose, the snapshot's
// Hotfix 8.5 term, 0 without one) and the quest log state (MixQuestState over the quest slots, with completion).
struct Progress
{
    std::uint32_t level = 0;
    std::uint32_t xp = 0;
    std::uint64_t gather = 0;
    std::uint32_t questState = 0;
};

// Which part moved (the first in this order), for the clear line; None = no progress.
enum class Moved : std::uint8_t
{
    None = 0,
    Level,
    Xp,
    Gather,
    QuestLog
};

inline char const* MovedName(Moved m)
{
    switch (m)
    {
        case Moved::None: return "none";
        case Moved::Level: return "level";
        case Moved::Xp: return "xp";
        case Moved::Gather: return "gather";
        case Moved::QuestLog: return "quest_log";
    }
    return "?";
}

inline Moved ProgressSince(Progress const& base, Progress const& now)
{
    if (now.level != base.level)
        return Moved::Level;
    if (now.xp != base.xp)
        return Moved::Xp;
    if (now.gather != base.gather)
        return Moved::Gather;
    if (now.questState != base.questState)
        return Moved::QuestLog;
    return Moved::None;
}

// The bot thread's memory of one rescue: the first check after the rescue takes the baseline, every later check compares
// against it. Lives on the bot thread only; the world thread only marks a rescue (an atomic flag the bot thread turns into
// Restart()) and reads whether the exclusion still holds (an atomic flag the bot thread clears).
class Memory
{
public:
    void Restart() { hasBase = false; }

    // The progress since the baseline (None on the baseline check itself).
    Moved Observe(Progress const& now)
    {
        if (!hasBase)
        {
            base = now;
            hasBase = true;
            return Moved::None;
        }
        return ProgressSince(base, now);
    }

private:
    bool hasBase = false;
    Progress base;
};

// The counts of one start-valley pass (for the log line): kept and each drop reason.
struct Tally
{
    std::uint32_t kept = 0;
    std::uint32_t nearby = 0;
    std::uint32_t duplicate = 0;
    std::uint32_t last = 0;

    void Add(Drop d)
    {
        switch (d)
        {
            case Drop::Kept: ++kept; break;
            case Drop::Near: ++nearby; break;
            case Drop::Duplicate: ++duplicate; break;
            case Drop::Last: ++last; break;
        }
    }
};
}
