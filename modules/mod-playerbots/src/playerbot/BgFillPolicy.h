#pragma once

#include <cstdint>

namespace ai::bgfill
{
// twow-repo#517 stage a (AiPlayerbot.BG.Enable + AiPlayerbot.BG.PlayerFill): pure rules of the manager's battleground
// fill (RandomPlayerbotMgr::CheckBgFill). A real player queues for WSG, AB or AV; persistent-roster bots fill both sides
// up to the template minimum, under a fail-closed cap of concurrent bot battlegrounds. Tested by
// t/bg_fill_policy_tests.cpp.

// The core's BattleGroundQueueTypeId values (src/game/Battlegrounds/BattleGround.h).
constexpr uint32_t QueueAV = 1;
constexpr uint32_t QueueWS = 2;
constexpr uint32_t QueueAB = 3;
constexpr uint32_t QueueBR = 4;   // Blood Ring 3v3: excluded (OB-00 D4)
constexpr uint32_t QueueSV = 5;   // Sunnyglade Valley: excluded in stage a (no tactics yet, its switch comes in stage d)

// AiPlayerbot.BG.CheckIntervalSeconds: 0 = this default, below the minimum = the minimum. The maximum only keeps the
// millisecond value from overflowing.
constexpr uint32_t DefaultCheckIntervalSeconds = 30;
constexpr uint32_t MinCheckIntervalSeconds = 10;
constexpr uint32_t MaxCheckIntervalSeconds = 3600;

// A bot joins no queue right after its login (BGJoinAction::isUseful uses the same 30 s).
constexpr uint32_t MinLoggedInSeconds = 30;

// Candidates collected per needed slot: a join the core refuses silently leaves a spare to try.
constexpr uint32_t CandidateFactor = 2;

// Only these queues are filled: WSG, AB and AV.
inline bool IsFillQueueType(uint32_t queueType)
{
    return queueType == QueueAV || queueType == QueueWS || queueType == QueueAB;
}

// Bots one side still needs so that a new battleground starts with the player: up to the template minimum, counting
// the real players of that side and the fill bots already queued for it. Both sides are filled to the minimum, so the
// factions stay balanced.
inline uint32_t NeedPerSide(uint32_t minPerTeam, uint32_t realSide, uint32_t pendingSide)
{
    uint32_t const have = realSide + pendingSide;
    return have >= minPerTeam ? 0 : minPerTeam - have;
}

// Fail closed: AiPlayerbot.BG.MaxConcurrent = 0 allows no bot battleground at all.
inline bool CapAllows(uint32_t running, uint32_t maxConcurrent)
{
    return running < maxConcurrent;
}

// Running bot battlegrounds as one bracket sees them: started ones with a roster bot plus brackets still forming (fill
// bots queued, not yet invited). A bracket that is forming itself does not count against its own top-up.
inline uint32_t RunningForBracket(uint32_t runningBgs, uint32_t formingBrackets, bool selfForming)
{
    return runningBgs + formingBrackets - (selfForming && formingBrackets ? 1 : 0);
}

inline uint32_t CheckIntervalMs(uint32_t cfgSeconds)
{
    uint32_t seconds = cfgSeconds ? cfgSeconds : DefaultCheckIntervalSeconds;
    if (seconds < MinCheckIntervalSeconds)
        seconds = MinCheckIntervalSeconds;
    if (seconds > MaxCheckIntervalSeconds)
        seconds = MaxCheckIntervalSeconds;
    return seconds * 1000;
}

// Unsigned difference, so a wrapped millisecond clock stays correct; last = 0 (never run) is due at once.
inline bool IntervalDue(uint32_t now, uint32_t last, uint32_t interval)
{
    return !last || now - last >= interval;
}

// How many candidates the bounded bot loop collects for a side that needs this many bots.
inline uint32_t CandidateLimit(uint32_t need)
{
    return need * CandidateFactor;
}
}
