#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ai::quest_groups
{
// twow-repo#627 S7 "Gruppenhilfe" (owner 18.10./23.10.2026, plan Y:\tw\evidence\ws-10\cli-grp\PLAN-T1.md §2): roster bots
// that hold the same elite or group quest form a 2..5 bot group, do it together, turn in together and go back to their
// own travel. Pure rules here - no game types, no config reads; the world-thread manager (QuestGroupWorld.cpp) feeds them.

// --- switch: AiPlayerbot.QuestGroups.Enable --------------------------------------------------------------------------------
// 0 = off, 1 = all bots, 2 = only cohort B (odd low guid), 3 = only cohort A (even low guid); any other value = off.
// Candidates and partners both pass it, so a group is always cohort-pure (in-realm A/B without crossover).
constexpr std::uint32_t ModeOff = 0;
constexpr std::uint32_t ModeAll = 1;
constexpr std::uint32_t ModeOnlyB = 2;
constexpr std::uint32_t ModeOnlyA = 3;

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    bool const b = (guidLow & 1u) != 0;
    return mode == ModeAll || (mode == ModeOnlyB && b) || (mode == ModeOnlyA && !b);
}

inline char Cohort(std::uint32_t guidLow)
{
    return (guidLow & 1u) ? 'B' : 'A';
}

// --- which quests: elite or group, not dungeon/raid/pvp (OB-20 #601) ----------------------------------------------------------
constexpr std::uint32_t QuestTypeElite = 1;
constexpr std::uint32_t QuestTypePvp = 41;
constexpr std::uint32_t QuestTypeRaid = 62;
constexpr std::uint32_t QuestTypeDungeon = 81;

inline bool IsGroupHelpQuest(std::uint32_t type, std::uint32_t suggestedPlayers, bool includeSuggestedPlayers)
{
    if (type == QuestTypeDungeon || type == QuestTypeRaid || type == QuestTypePvp)
        return false;
    return type == QuestTypeElite || (includeSuggestedPlayers && suggestedPlayers >= 2);
}

// A bot waits for help on a quest it holds open (not complete, not turned in), that is not red (owner 18.10.: red
// quests >= L+5 stay skipped), not abandoned by S1, and still has an open objective of its own.
struct QuestFacts
{
    bool incomplete = false;
    bool classQuest = false;
    std::uint32_t questLevel = 0;
    std::uint32_t botLevel = 0;
    bool abandoned = false;
    std::uint32_t openObjectives = 0;
};

inline bool Waits(QuestFacts const& f)
{
    return f.incomplete && !f.classQuest && !f.abandoned && f.openObjectives > 0 && f.questLevel < f.botLevel + 5;
}

// --- forming: one quest, one map; pure greedy, no pair values ----------------------------------------------------------------
struct Candidate
{
    std::uint32_t guid = 0;
    std::uint32_t level = 0;
    std::uint32_t guild = 0;          // 0 = none
    std::uint32_t openObjectives = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct FormLimits
{
    std::uint32_t maxSize = 5;        // QuestGroups.MaxSize (clamped 2..5)
    std::uint32_t levelWindow = 3;    // max - min inside the group
    float recruitRadius = 800.0f;     // yards from the leader, same map
};

inline float Dist2(Candidate const& a, Candidate const& b)
{
    float const dx = a.x - b.x;
    float const dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Groups out of the waiting candidates of one quest on one map. Leader = most open objectives (OB-10), ties: lower guid.
// Members: in the radius, the group's level spread within the window, same guild first (owner: guild help), then the
// nearest. Groups below 2 are not formed; a candidate is in at most one group. Index 0 of each group is the leader.
inline std::vector<std::vector<Candidate>> Form(std::vector<Candidate> pool, FormLimits const& lim)
{
    std::vector<std::vector<Candidate>> groups;
    std::uint32_t const maxSize = std::max<std::uint32_t>(2, std::min<std::uint32_t>(5, lim.maxSize));
    float const r2 = lim.recruitRadius * lim.recruitRadius;
    std::sort(pool.begin(), pool.end(), [](Candidate const& a, Candidate const& b)
    {
        return a.openObjectives != b.openObjectives ? a.openObjectives > b.openObjectives : a.guid < b.guid;
    });
    std::vector<bool> used(pool.size(), false);
    for (std::size_t s = 0; s < pool.size(); ++s)
    {
        if (used[s])
            continue;
        Candidate const& leader = pool[s];
        std::vector<std::size_t> near;
        for (std::size_t i = 0; i < pool.size(); ++i)
            if (i != s && !used[i] && Dist2(leader, pool[i]) <= r2)
                near.push_back(i);
        std::sort(near.begin(), near.end(), [&](std::size_t a, std::size_t b)
        {
            bool const ga = leader.guild && pool[a].guild == leader.guild;
            bool const gb = leader.guild && pool[b].guild == leader.guild;
            if (ga != gb)
                return ga;
            float const da = Dist2(leader, pool[a]);
            float const db = Dist2(leader, pool[b]);
            return da != db ? da < db : pool[a].guid < pool[b].guid;
        });
        std::vector<Candidate> group{ leader };
        std::uint32_t lo = leader.level;
        std::uint32_t hi = leader.level;
        for (std::size_t i : near)
        {
            if (group.size() >= maxSize)
                break;
            std::uint32_t const nlo = std::min(lo, pool[i].level);
            std::uint32_t const nhi = std::max(hi, pool[i].level);
            if (nhi - nlo > lim.levelWindow)
                continue;
            group.push_back(pool[i]);
            lo = nlo;
            hi = nhi;
        }
        if (group.size() < 2)
            continue;
        used[s] = true;
        for (Candidate const& c : group)
            for (std::size_t i = 0; i < pool.size(); ++i)
                if (pool[i].guid == c.guid)
                    used[i] = true;
        groups.push_back(std::move(group));
    }
    return groups;
}

// --- life cycle ---------------------------------------------------------------------------------------------------------------
enum class Phase
{
    Rendezvous,  // members walk to the leader
    Goal,        // the leader goes to the open objective, members follow and assist
    TurnIn,      // every member's quest is complete: to the quest taker, each turns in
};

enum class Dissolve
{
    None,
    TurnedIn,           // every member turned the quest in (the normal end)
    RendezvousTimeout,  // RendezvousMinutes passed and the group never gathered
    NoProgress,         // no objective progress / kill credit of any member for NoProgressMinutes (combat never resets it)
    MaxTime,            // MaxQuestMinutes since forming
    TurnInWait,         // TurnInWaitMinutes in the turn-in phase: whoever is done turns in alone
    LeaderLost,         // the leader left, logged out or died twice, and no member can take over
    LeaderStuck,        // the leader was moved by PosStuck (park/unstuck/home bind)
    RealPlayer,         // a member's master became a real player (contract pin: real-player groups are never touched)
    TooSmall,           // fewer than 2 members left
    Abandoned,          // the quest was dropped or abandoned by a member that leads it
};

inline char const* DissolveName(Dissolve d)
{
    switch (d)
    {
        case Dissolve::None:              return "none";
        case Dissolve::TurnedIn:          return "turned_in";
        case Dissolve::RendezvousTimeout: return "rendezvous_timeout";
        case Dissolve::NoProgress:        return "no_progress";
        case Dissolve::MaxTime:           return "max_time";
        case Dissolve::TurnInWait:        return "turnin_wait";
        case Dissolve::LeaderLost:        return "leader_lost";
        case Dissolve::LeaderStuck:       return "leader_stuck";
        case Dissolve::RealPlayer:        return "real_player";
        case Dissolve::TooSmall:          return "too_small";
        case Dissolve::Abandoned:         return "abandoned";
    }
    return "unknown";
}

inline char const* PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Rendezvous: return "rendezvous";
        case Phase::Goal:       return "goal";
        case Phase::TurnIn:     return "turnin";
    }
    return "unknown";
}

struct Timers
{
    std::uint32_t rendezvousMinutes = 10;
    std::uint32_t noProgressMinutes = 15;
    std::uint32_t turnInWaitMinutes = 5;
    std::uint32_t maxQuestMinutes = 60;
};

struct GroupFacts
{
    Phase phase = Phase::Rendezvous;
    std::uint32_t now = 0;
    std::uint32_t formedAt = 0;
    std::uint32_t phaseSince = 0;
    std::uint32_t lastProgress = 0;   // last objective counter change of any member (or forming)
    std::uint32_t members = 0;
    bool realPlayer = false;
    bool leaderLost = false;
    bool leaderStuck = false;
    bool abandoned = false;
    bool gathered = false;            // every member within the gather radius of the leader
    bool allComplete = false;         // every member's quest is complete (or turned in)
    bool allTurnedIn = false;         // every member turned the quest in
};

struct Step
{
    Dissolve dissolve = Dissolve::None;
    Phase next = Phase::Rendezvous;
};

// One manager pass for one group. Order: safety first (real player), then the end states, then the timers.
inline Step Decide(GroupFacts const& f, Timers const& t)
{
    Step s;
    s.next = f.phase;
    if (f.realPlayer)            { s.dissolve = Dissolve::RealPlayer; return s; }
    if (f.allTurnedIn)           { s.dissolve = Dissolve::TurnedIn; return s; }
    if (f.members < 2)           { s.dissolve = Dissolve::TooSmall; return s; }
    if (f.abandoned)             { s.dissolve = Dissolve::Abandoned; return s; }
    if (f.leaderStuck)           { s.dissolve = Dissolve::LeaderStuck; return s; }
    if (f.leaderLost)            { s.dissolve = Dissolve::LeaderLost; return s; }
    if (f.now - f.formedAt >= t.maxQuestMinutes * 60) { s.dissolve = Dissolve::MaxTime; return s; }

    switch (f.phase)
    {
        case Phase::Rendezvous:
            if (f.allComplete)
                s.next = Phase::TurnIn;
            else if (f.gathered)
                s.next = Phase::Goal;
            else if (f.now - f.phaseSince >= t.rendezvousMinutes * 60)
                s.dissolve = Dissolve::RendezvousTimeout;
            break;
        case Phase::Goal:
            if (f.allComplete)
                s.next = Phase::TurnIn;
            else if (f.now - f.lastProgress >= t.noProgressMinutes * 60)
                s.dissolve = Dissolve::NoProgress;   // NoProgress always fires: only objective progress resets it
            break;
        case Phase::TurnIn:
            if (f.now - f.phaseSince >= t.turnInWaitMinutes * 60)
                s.dissolve = Dissolve::TurnInWait;
            break;
    }
    return s;
}

// --- per (bot, quest): queue age and attempts ---------------------------------------------------------------------------------
// The manager is the only writer (world thread); CLI-Deep's hooks (S5a trigger, QFO) read the queue age from map threads,
// so the store is mutex-protected and read rarely (not per tick). Bounded: a full store drops its oldest half.
class QueueStore
{
public:
    static constexpr std::size_t MaxEntries = 16384;

    struct Entry
    {
        std::uint32_t firstSeen = 0;   // first pass the bot waited on the quest
        std::uint32_t lastSeen = 0;
        std::uint32_t attempts = 0;    // groups formed for it that did not end with turned_in
        std::uint32_t blockedUntil = 0;
    };

    void Seen(std::uint32_t guid, std::uint32_t quest, std::uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex);
        Bound(now);
        Entry& e = entries[Key(guid, quest)];
        if (!e.firstSeen)
            e.firstSeen = now;
        e.lastSeen = now;
    }

    void Failed(std::uint32_t guid, std::uint32_t quest, std::uint32_t now, std::uint32_t cooldownSeconds)
    {
        std::lock_guard<std::mutex> lock(mutex);
        Entry& e = entries[Key(guid, quest)];
        ++e.attempts;
        e.blockedUntil = now + cooldownSeconds;
    }

    void Forget(std::uint32_t guid, std::uint32_t quest)
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(Key(guid, quest));
    }

    // Entries of a bot not seen in this pass any more (quest done, dropped, bot gone).
    void ForgetStale(std::uint32_t now, std::uint32_t maxAgeSeconds)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = entries.begin(); it != entries.end();)
            it = (now - it->second.lastSeen > maxAgeSeconds) ? entries.erase(it) : std::next(it);
    }

    bool Blocked(std::uint32_t guid, std::uint32_t quest, std::uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = entries.find(Key(guid, quest));
        return it != entries.end() && it->second.blockedUntil > now;
    }

    std::uint32_t Attempts(std::uint32_t guid, std::uint32_t quest) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = entries.find(Key(guid, quest));
        return it == entries.end() ? 0 : it->second.attempts;
    }

    // Seconds the bot has waited on the quest (0 = not waiting).
    std::uint32_t QueuedAge(std::uint32_t guid, std::uint32_t quest, std::uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = entries.find(Key(guid, quest));
        return it == entries.end() || !it->second.firstSeen || now < it->second.firstSeen ? 0 : now - it->second.firstSeen;
    }

    // The oldest wait of the bot over all its quests (0 = none) - one call per bot for the S5a band trigger.
    std::uint32_t OldestQueuedAge(std::uint32_t guid, std::uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::uint32_t oldest = 0;
        for (auto const& [key, e] : entries)
            if (std::uint32_t(key >> 32) == guid && e.firstSeen && now >= e.firstSeen)
                oldest = std::max(oldest, now - e.firstSeen);
        return oldest;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entries.size();
    }

private:
    static std::uint64_t Key(std::uint32_t guid, std::uint32_t quest)
    {
        return (std::uint64_t(guid) << 32) | quest;
    }

    void Bound(std::uint32_t now)
    {
        if (entries.size() < MaxEntries)
            return;
        std::vector<std::uint32_t> seen;
        seen.reserve(entries.size());
        for (auto const& kv : entries)
            seen.push_back(kv.second.lastSeen);
        std::nth_element(seen.begin(), seen.begin() + seen.size() / 2, seen.end());
        std::uint32_t const cut = seen[seen.size() / 2];
        for (auto it = entries.begin(); it != entries.end();)
            it = it->second.lastSeen <= cut && it->second.lastSeen != now ? entries.erase(it) : std::next(it);
    }

    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, Entry> entries;
};
}
