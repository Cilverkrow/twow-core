#pragma once

#include <cstdint>
#include <iterator>
#include <mutex>
#include <unordered_map>

namespace ai::adhoc_group
{
// twow-repo#365 step 2 (owner contract 2026-09-27, design docs/design/bot-groups.md
// section 3): roster bots working on the same quest objective close to each other
// form an ad-hoc group (up to BotGroups.MaxBots), share kill credit and leave as
// soon as their own objective is done. Nobody's quest log changes.

// The objective a bot works on: quest and objective index (0..3).
struct ObjectiveKey
{
    std::uint32_t questId = 0;
    std::uint8_t objective = 0;

    bool IsValid() const { return questId != 0; }
    bool operator==(ObjectiveKey const& other) const { return questId == other.questId && objective == other.objective; }
    bool operator!=(ObjectiveKey const& other) const { return !(*this == other); }
};

// Who sends the invite between bot A (scanning) and neighbour B. A bot in a group
// that is not an ad-hoc group it leads never invites or is invited; the leader of
// an ad-hoc group invites, and between two ungrouped bots the lower GUID invites,
// so two bots never invite each other at the same moment.
enum class Invite
{
    None,
    AInvitesB,
    BInvitesA,
};

inline Invite Decide(std::uint32_t aGuid, bool aGrouped, bool aLeadsAdhoc,
    std::uint32_t bGuid, bool bGrouped, bool bLeadsAdhoc)
{
    if ((aGrouped && !aLeadsAdhoc) || (bGrouped && !bLeadsAdhoc))
        return Invite::None;
    if (aGrouped && bGrouped)
        return Invite::None;  // two ad-hoc groups are not merged
    if (aLeadsAdhoc)
        return Invite::AInvitesB;
    if (bLeadsAdhoc)
        return Invite::BInvitesA;
    return aGuid < bGuid ? Invite::AInvitesB : Invite::BInvitesA;
}

inline bool LevelWindowOk(std::uint32_t minLevel, std::uint32_t maxLevel, std::uint32_t window)
{
    return maxLevel >= minLevel && maxLevel - minLevel <= window;
}

// Leave rules (design 3.2), in the order they are checked.
enum class Leave
{
    None,
    QuestTurnedIn,  // the quest is no longer in progress in the log
    ObjectiveDone,  // the own objective counter is complete
    Instance,       // the bot or its group entered an instance
    OutOfRange,     // more than 2 x Radius from the leader for OutOfRangeSeconds
    LevelWindow,    // the spread left the window for LevelWindowSeconds
    Idle,           // no objective progress for IdleSeconds
};

constexpr std::uint32_t OutOfRangeSeconds = 60;
constexpr std::uint32_t LevelWindowSeconds = 300;
constexpr std::uint32_t IdleSeconds = 600;

struct LeaveFacts
{
    bool questInProgress = true;
    bool objectiveDone = false;
    bool inInstance = false;
    std::uint32_t now = 0;
    std::uint32_t outOfRangeSince = 0;   // 0 = in range
    std::uint32_t levelWindowSince = 0;  // 0 = within the window
    std::uint32_t lastProgress = 0;      // last objective progress (or joining)
};

inline Leave DecideLeave(LeaveFacts const& f)
{
    if (!f.questInProgress)
        return Leave::QuestTurnedIn;
    if (f.objectiveDone)
        return Leave::ObjectiveDone;
    if (f.inInstance)
        return Leave::Instance;
    if (f.outOfRangeSince && f.now - f.outOfRangeSince >= OutOfRangeSeconds)
        return Leave::OutOfRange;
    if (f.levelWindowSince && f.now - f.levelWindowSince >= LevelWindowSeconds)
        return Leave::LevelWindow;
    if (f.lastProgress && f.now - f.lastProgress >= IdleSeconds)
        return Leave::Idle;
    return Leave::None;
}

inline char const* LeaveName(Leave reason)
{
    switch (reason)
    {
        case Leave::None:          return "none";
        case Leave::QuestTurnedIn: return "quest_turned_in";
        case Leave::ObjectiveDone: return "objective_done";
        case Leave::Instance:      return "instance";
        case Leave::OutOfRange:    return "out_of_range";
        case Leave::LevelWindow:   return "level_window";
        case Leave::Idle:          return "idle";
    }
    return "unknown";
}

// Pair cooldown (design 3.3): bounded, mutex-protected (bots of different maps
// are updated on different threads, #351), no AI context values.
class PairCooldownStore
{
public:
    static constexpr std::size_t MaxPairs = 8192;

    void Block(std::uint32_t a, std::uint32_t b, std::uint32_t until, std::uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pairs.size() >= MaxPairs)
        {
            for (auto it = pairs.begin(); it != pairs.end();)
                it = it->second <= now ? pairs.erase(it) : std::next(it);
            if (pairs.size() >= MaxPairs)
                pairs.clear();
        }
        pairs[Key(a, b)] = until;
    }

    bool IsBlocked(std::uint32_t a, std::uint32_t b, std::uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = pairs.find(Key(a, b));
        return it != pairs.end() && it->second > now;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return pairs.size();
    }

private:
    static std::uint64_t Key(std::uint32_t a, std::uint32_t b)
    {
        return a < b ? (std::uint64_t(a) << 32) | b : (std::uint64_t(b) << 32) | a;
    }

    mutable std::mutex mutex;
    std::unordered_map<std::uint64_t, std::uint32_t> pairs;
};

// Which groups are ad-hoc groups, and for which objective (design 3.1). In memory
// only: after a restart a leftover group is an ordinary bot group.
class Registry
{
public:
    static constexpr std::size_t MaxGroups = 4096;
    static constexpr std::uint32_t MaxAgeSeconds = 4 * 3600;

    struct Entry
    {
        ObjectiveKey key;
        std::uint32_t created = 0;
    };

    void Register(std::uint32_t groupId, ObjectiveKey key, std::uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (groups.size() >= MaxGroups)
        {
            for (auto it = groups.begin(); it != groups.end();)
                it = now - it->second.created > MaxAgeSeconds ? groups.erase(it) : std::next(it);
            if (groups.size() >= MaxGroups)
                groups.clear();
        }
        groups[groupId] = { key, now };
    }

    bool Find(std::uint32_t groupId, Entry& entry) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const it = groups.find(groupId);
        if (it == groups.end())
            return false;
        entry = it->second;
        return true;
    }

    void Forget(std::uint32_t groupId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        groups.erase(groupId);
    }

private:
    mutable std::mutex mutex;
    std::unordered_map<std::uint32_t, Entry> groups;
};

inline PairCooldownStore& PairCooldowns()
{
    static PairCooldownStore store;
    return store;
}

inline Registry& Groups()
{
    static Registry registry;
    return registry;
}
}
