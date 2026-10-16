#pragma once

#include <cstdint>
#include <vector>

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

// A fill bot not invited after this long leaves its queue again (the next interval queues fresh ones while the real
// player still waits): it holds a FIFO place and keeps its bracket "forming" against the cap.
constexpr uint32_t PendingMaxSeconds = 300;

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

// The core's own queue minimum (AV: Alterac.MinPlayersInQueue uninvited entries per side, 0 = off) above the template
// minimum: the core shuffles that many entries and takes only the template minimum, so a queued real player could be
// left out of a match the fill bots start. The fill stays out then (fail closed).
inline bool QueueMinBlocksFill(uint32_t templateMin, uint32_t queueMin)
{
    return queueMin > templateMin;
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

// A pending fill bot queued at joinMs has waited too long (unsigned difference: a wrapped clock stays correct).
inline bool PendingExpired(uint32_t now, uint32_t joinMs, uint32_t maxMs)
{
    return now - joinMs >= maxMs;
}

// How many candidates the bounded bot loop collects for a side that needs this many bots.
inline uint32_t CandidateLimit(uint32_t need)
{
    return need * CandidateFactor;
}

// twow-repo#517 stage b (AiPlayerbot.BG.SlotRelease): a real player who has waited SlotReleaseWaitSeconds uninvited
// gets into a running battleground that holds fill bots - first by the pair option (he and one fill bot of the other
// faction are invited together, OB-00 D2), else a fill bot of his side leaves and his slot is reserved for him (only
// from a side above the minimum, so no PrematureFinish, G2/OB-20).

// AiPlayerbot.BG.SlotReleaseWaitSeconds: 0 = this default, below the minimum = the minimum.
constexpr uint32_t DefaultSlotReleaseWaitSeconds = 60;
constexpr uint32_t MinSlotReleaseWaitSeconds = 15;
// AiPlayerbot.BG.BotOnlyGraceSeconds: a started-for-fill battleground without a real player for this long ends.
constexpr uint32_t DefaultBotOnlyGraceSeconds = 120;
constexpr uint32_t MinBotOnlyGraceSeconds = 60;
// Keeps the millisecond values of both from overflowing.
constexpr uint32_t MaxStageBSeconds = 3600;

// FillPlayersToBG's allowed difference of the free slots after an invite (InvitationType 1): AV 3, else 0.
constexpr uint32_t DifferentialAV = 3;

inline uint32_t ClampSeconds(uint32_t cfgSeconds, uint32_t defaultSeconds, uint32_t minSeconds)
{
    uint32_t seconds = cfgSeconds ? cfgSeconds : defaultSeconds;
    if (seconds < minSeconds)
        seconds = minSeconds;
    if (seconds > MaxStageBSeconds)
        seconds = MaxStageBSeconds;
    return seconds;
}

inline uint32_t SlotReleaseWaitMs(uint32_t cfgSeconds)
{
    return ClampSeconds(cfgSeconds, DefaultSlotReleaseWaitSeconds, MinSlotReleaseWaitSeconds) * 1000;
}

inline uint32_t BotOnlyGraceMs(uint32_t cfgSeconds)
{
    return ClampSeconds(cfgSeconds, DefaultBotOnlyGraceSeconds, MinBotOnlyGraceSeconds) * 1000;
}

// A deadline (reservation end) has passed; the signed difference keeps a wrapped millisecond clock correct.
inline bool DeadlinePassed(uint32_t now, uint32_t until)
{
    return int32_t(now - until) >= 0;
}

// Pair option (OB-00 D2): both sides of the running battleground have a free slot (the core's free slots count the
// invited players, BattleGround::GetFreeSlotsForTeam), so the player and one fill bot of the other faction fit.
inline bool PairPossible(uint32_t countA, uint32_t countH, uint32_t maxPerTeam)
{
    return countA < maxPerTeam && countH < maxPerTeam;
}

// Slot release only from a side above the minimum: it stays at or above the minimum while the player's invite is
// open, so the core's PrematureFinish countdown does not start (G2, OB-20).
inline bool ReleaseAllowed(uint32_t countSide, uint32_t minPerTeam)
{
    return countSide > minPerTeam;
}

// With InvitationType 1 the core invites a lone player into a running battleground only if the free slots of both
// sides differ by at most the differential afterwards (BattleGroundQueue::FillPlayersToBG). After one release on his
// side that holds when the invited counts differ by at most the differential before it; otherwise the release would
// only shrink his side and he would still wait.
inline bool ReleaseInvitesPlayer(uint32_t invitedSide, uint32_t invitedOther, uint32_t differential)
{
    uint32_t const diff = invitedSide > invitedOther ? invitedSide - invitedOther : invitedOther - invitedSide;
    return diff <= differential;
}

// Target battleground for a waiting player: the most fill bots on his side first, then the lowest instance id.
inline bool PreferTarget(uint32_t fillOnSide, uint32_t instanceId, uint32_t bestFillOnSide, uint32_t bestInstanceId)
{
    if (fillOnSide != bestFillOnSide)
        return fillOnSide > bestFillOnSide;
    return instanceId < bestInstanceId;
}

// A manager-owned fill bot on the player's side of the target battleground.
struct ReleaseCand
{
    uint32_t guid = 0;
    uint32_t joinMs = 0;        // when it joined the battleground (accept)
    bool carriesFlag = false;   // WSG flag aura or the battleground's flag picker
    bool inCombat = false;
    bool alive = true;
};

// The bot that leaves for the waiting player: never a flag carrier, never a dead one; out of combat before in combat;
// then the latest joined (age from nowMs, so a wrapped clock stays correct); then the lowest guid. 0 = no victim.
inline uint32_t ChooseReleaseVictim(std::vector<ReleaseCand> const& cands, uint32_t nowMs)
{
    ReleaseCand const* best = nullptr;
    for (ReleaseCand const& c : cands)
    {
        if (!c.guid || c.carriesFlag || !c.alive)
            continue;
        if (!best)
        {
            best = &c;
            continue;
        }
        if (c.inCombat != best->inCombat)
        {
            if (!c.inCombat)
                best = &c;
            continue;
        }
        uint32_t const age = nowMs - c.joinMs;
        uint32_t const bestAge = nowMs - best->joinMs;
        if (age != bestAge)
        {
            if (age < bestAge)
                best = &c;
            continue;
        }
        if (c.guid < best->guid)
            best = &c;
    }
    return best ? best->guid : 0;
}
}
