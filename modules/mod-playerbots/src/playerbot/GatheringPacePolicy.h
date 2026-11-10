#pragma once

#include <cstdint>

namespace ai::gathering_pace
{
// twow-repo#333 T28b (plan A3-PLAN-ZIEL T28b, OB-00 09.11.2026 T28 Q1 (a); AiPlayerbot.Gathering.PaceReach, default 0): the
// mining / herbalism reach. A roster bot on its own whose herbalism or mining lags behind min(rank cap, 5 x level) may pick
// an orange or yellow node up to PaceReachYards away instead of the loot distance (25 yd). The rule lives in the one shared
// LootObjectStack::OrderByDistance, so the trigger, LootAction and both travel pauses agree. Pure rules and the per-bot state;
// no game types, no config, no logging here.
//
// Far pick = a node beyond the loot distance that only this lever made eligible. Limits: at most FarPicksPerHour new far picks
// in any HourSeconds (sliding); a far node needs 0 strong hostiles within HostileYards of it (cached HostileCacheSeconds per
// node); a far pick not opened TimeoutSeconds after the pick (LootAction set the loot target), or with a strong hostile cached
// near its node, ends while the bot is still beyond the loot distance: the caller resets the loot target and the node goes on
// the miss list (MissSlots entries, MissSeconds each). Within the loot distance the near rules apply as before.
constexpr uint32_t kHerbalism = 182;
constexpr uint32_t kMining = 186;
constexpr uint32_t DefaultYards = 45;
constexpr uint32_t MinYards = 30;
constexpr uint32_t MaxYards = 60;
constexpr uint32_t FarPicksPerHour = 12;
constexpr uint32_t HourSeconds = 3600;
constexpr uint32_t TimeoutSeconds = 20;
constexpr uint32_t MissSlots = 8;
constexpr uint32_t MissSeconds = 600;
constexpr uint32_t HostileCacheSeconds = 10;
constexpr uint32_t HostileCacheSlots = 10;   // the loot stack holds at most 10 entries
constexpr float HostileYards = 30.0f;
constexpr uint32_t StrongLevelGap = 7;

// The mode (AiPlayerbot.Gathering.PaceReach) is a cohort mode: 0 off, 1 all, 2 cohort B, 3 cohort A (CohortPolicy.h); the
// loader turns any other value into 0. PaceReachYards: inside [MinYards, MaxYards], else the default.
inline bool YardsValid(uint32_t raw)
{
    return raw >= MinYards && raw <= MaxYards;
}

inline uint32_t Yards(uint32_t raw)
{
    return YardsValid(raw) ? raw : DefaultYards;
}

// Herbalism and mining only: no skinning, no cooking, survival or fishing (owner W2), no lockpicking.
inline bool IsPaceSkill(uint32_t skill)
{
    return skill == kHerbalism || skill == kMining;
}

// The lag rule: the skill is below min(rank cap, 5 x level), the target of the gather purpose. Not learned (cap 0) never lags.
inline bool Behind(uint32_t value, uint32_t skillMax, uint32_t levelMax)
{
    uint32_t const target = skillMax < levelMax ? skillMax : levelMax;
    return value < target;
}

// Orange or yellow node: the skill is below its required value + 50 (green starts there, grey at + 100). Far picks only.
inline bool FarColorOk(uint32_t reqSkillValue, uint32_t value)
{
    return value < reqSkillValue + 50;
}

// A strong hostile: not at least StrongLevelGap + 1 levels below the bot (the queue-time rule of AddGatheringLootAction).
inline bool Strong(uint32_t botLevel, uint32_t hostileLevel)
{
    return !(botLevel > hostileLevel + StrongLevelGap);
}

// The far-pick state of one bot (lives on its loot stack, the bot's own thread).
struct State
{
    struct Miss
    {
        uint64_t guid = 0;
        uint32_t until = 0;
    };
    struct Hostiles
    {
        uint64_t guid = 0;
        uint32_t at = 0;
        bool clear = false;
    };

    Miss misses[MissSlots];
    uint32_t nextMiss = 0;
    Hostiles hostiles[HostileCacheSlots];
    uint32_t nextHostiles = 0;
    uint64_t pending = 0;      // the far pick waiting for its open (0 = none)
    uint32_t pickedAt = 0;
    uint32_t pickTimes[FarPicksPerHour] = {};   // the last far picks, oldest at nextPick (0 = free)
    uint32_t nextPick = 0;

    bool Missed(uint64_t guid, uint32_t now) const
    {
        for (Miss const& miss : misses)
            if (miss.guid == guid && now < miss.until)
                return true;
        return false;
    }

    void AddMiss(uint64_t guid, uint32_t now)
    {
        for (Miss& miss : misses)
            if (miss.guid == guid)
            {
                miss.until = now + MissSeconds;
                return;
            }
        misses[nextMiss] = Miss{ guid, now + MissSeconds };
        nextMiss = (nextMiss + 1) % MissSlots;
    }

    // Sliding budget: a new far pick is allowed while fewer than FarPicksPerHour picks lie in the last HourSeconds, i.e. the
    // oldest of the last FarPicksPerHour picks is free or at least HourSeconds old.
    bool BudgetLeft(uint32_t now) const
    {
        uint32_t const oldest = pickTimes[nextPick];
        return !oldest || now - oldest >= HourSeconds;
    }

    uint32_t PicksLastHour(uint32_t now) const
    {
        uint32_t n = 0;
        for (uint32_t at : pickTimes)
            if (at && now - at < HourSeconds)
                ++n;
        return n;
    }

    bool PendingExpired(uint64_t guid, uint32_t now) const
    {
        return pending && pending == guid && now - pickedAt >= TimeoutSeconds;
    }

    // May this node be a far pick now: not missed, not a timed-out pick, and either the pick already counted or budget left.
    bool FarOk(uint64_t guid, uint32_t now) const
    {
        if (!guid || Missed(guid, now) || PendingExpired(guid, now))
            return false;
        return pending == guid || BudgetLeft(now);
    }

    // The cached hostile check of a node: -1 unknown or older than HostileCacheSeconds, 0 a strong hostile, 1 clear.
    int HostilesClear(uint64_t guid, uint32_t now) const
    {
        for (Hostiles const& entry : hostiles)
            if (entry.guid == guid && now - entry.at < HostileCacheSeconds)
                return entry.clear ? 1 : 0;
        return -1;
    }

    void PutHostiles(uint64_t guid, uint32_t now, bool clear)
    {
        for (Hostiles& entry : hostiles)
            if (entry.guid == guid)
            {
                entry.at = now;
                entry.clear = clear;
                return;
            }
        hostiles[nextHostiles] = Hostiles{ guid, now, clear };
        nextHostiles = (nextHostiles + 1) % HostileCacheSlots;
    }

    // LootAction set the loot target to a far node: the clock starts here. true = a new far pick (counted once per pick). The
    // caller lets a different pending pick go first (Leave).
    bool Pick(uint64_t guid, uint32_t now)
    {
        if (!guid || pending == guid)
            return false;
        pickTimes[nextPick] = now;
        nextPick = (nextPick + 1) % FarPicksPerHour;
        pending = guid;
        pickedAt = now;
        return true;
    }

    // LootAction set a near target: a pending far pick goes; true = it had timed out (it goes on the miss list).
    bool Leave(uint32_t now)
    {
        if (!pending)
            return false;
        bool const timedOut = now - pickedAt >= TimeoutSeconds;
        if (timedOut)
            AddMiss(pending, now);
        pending = 0;
        return timedOut;
    }

    // The open of this node started: true = it was the pending far pick.
    bool Opened(uint64_t guid)
    {
        if (!pending || pending != guid)
            return false;
        pending = 0;
        return true;
    }

    // The pending far pick ends without an open: TimeoutSeconds after its pick, or the cached check saw a strong hostile near
    // its node. The caller skips it while the bot is within the loot distance of the node (the near rules apply there).
    bool EndDue(uint64_t guid, uint32_t now) const
    {
        return PendingExpired(guid, now) || (pending && pending == guid && HostilesClear(guid, now) == 0);
    }

    // The caller ends the far pick (it resets the loot target): the node goes on the miss list.
    void End(uint64_t guid, uint32_t now)
    {
        AddMiss(guid, now);
        pending = 0;
    }
};
}
