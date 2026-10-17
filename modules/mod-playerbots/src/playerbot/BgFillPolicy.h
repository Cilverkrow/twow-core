#pragma once

#include <cstdint>
#include <string>
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
// from a side above the minimum, so no PrematureFinish, G2/OB-20). A real player's queue group of k members counts as
// a whole (owner 16.10.): k pair bots (more while his side is ahead, PairCount), or up to k releases in one decision.

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

// Owner 16.10. (OB-00): a real player queued as a group with his own bots has the same priority as a solo player. The
// core queues and invites the group as one entry (GroupQueueInfo) of size k, never split, so the rules below take the
// entry's size; a solo player is k = 1 and gets exactly the stage-b results. The core takes no group joins for AV
// (HandleBattlemasterJoinOpcode), so an AV entry is always k = 1.

// Pair option (OB-00 D2): both sides of the running battleground have k free slots (the core's free slots count the
// invited players, BattleGround::GetFreeSlotsForTeam), so the player's entry and k fill bots of the other faction fit.
inline bool PairPossible(uint32_t countA, uint32_t countH, uint32_t maxPerTeam, uint32_t k = 1)
{
    return k <= maxPerTeam && countA <= maxPerTeam - k && countH <= maxPerTeam - k;
}

// Pair bots for the entry of size k (follow-up review): with his side more than the differential ahead, k bots leave
// the free slots uneven, so the core's balance loop (FillPlayersToBG) kicks his entry and invites only some of the bots;
// the rest stay queued for him and block the next pair until they expire. The bots by which his side is ahead beyond
// the differential go along, so the free slots are even after the invite and the core invites his entry and all of
// them together. PairPossible keeps the room: invitedOther + count = invitedSide + k - differential <= maxPerTeam.
inline uint32_t PairCount(uint32_t invitedSide, uint32_t invitedOther, uint32_t differential, uint32_t k = 1)
{
    uint32_t const ahead = invitedSide > invitedOther ? invitedSide - invitedOther : 0;
    return k + (ahead > differential ? ahead - differential : 0);
}

// Slot release only from a side above the minimum: after its releases it stays at or above the minimum while the
// player's invite is open, so the core's PrematureFinish countdown does not start (G2, OB-20).
inline bool ReleaseAllowed(uint32_t countSide, uint32_t minPerTeam, uint32_t releases = 1)
{
    return countSide > minPerTeam && countSide - minPerTeam >= releases;
}

// The core's FillPlayersToBG with InvitationType 1, for one entry of size k on the player's side and nothing else
// selected (fill bots of his side still queued ahead of him leave first, ScanBgSlotRelease): the entry is selected when
// his side has k free slots (SelectionPool::AddGroup). Then, with diffSide = freeSide - k and diffOther = freeOther:
// |diffSide - diffOther| <= differential invites it; diffSide > diffOther + differential kicks the empty other pool and
// breaks with the entry still selected (aliFree <= diffHorde + 1), which invites it too; diffSide < diffOther -
// differential kicks the entry. So invited exactly when freeSide >= k and invitedOther - invitedSide >= k - differential
// (free = max - invited).
inline bool CoreInvitesEntry(uint32_t invitedSide, uint32_t invitedOther, uint32_t maxPerTeam, uint32_t differential, uint32_t k)
{
    if (!k || invitedSide > maxPerTeam || maxPerTeam - invitedSide < k)
        return false;
    return int64_t(invitedOther) - int64_t(invitedSide) >= int64_t(k) - int64_t(differential);
}

// With InvitationType 1 the core invites the player's entry alone into a running battleground only if the free slots of
// both sides differ by at most the differential afterwards (CoreInvitesEntry). After k releases on his side that holds
// when his side is at most the differential ahead before them. The other side may be at most differential + k - 1
// ahead: with the WSG/AB differential 0 that is exactly "a release is needed at all" (the other side less than k ahead,
// else the core invites the entry without one); for k = 1 both halves give the stage-b rule, the invited counts differ
// by at most the differential. Otherwise the release would only shrink his side and he would still wait.
inline bool ReleaseInvitesPlayer(uint32_t invitedSide, uint32_t invitedOther, uint32_t differential, uint32_t k = 1)
{
    if (invitedSide >= invitedOther)
        return invitedSide - invitedOther <= differential;
    return invitedOther - invitedSide <= differential + (k ? k - 1 : 0);
}

// Releases from his side for one decision: the fewest (1..k) after which the core invites his entry of size k alone
// (CoreInvitesEntry), 0 = no release gets it in (ReleaseInvitesPlayer, or not enough bots on his side) or none is
// needed (the core invites the entry without one - with the AV differential his side may be ahead and still get in; a
// release would only shrink it, follow-up review). For k = 1 that is 1 exactly when ReleaseInvitesPlayer holds and the
// core does not invite him already. Bounded by k (at most MaxPlayersPerTeam).
inline uint32_t ReleaseCount(uint32_t invitedSide, uint32_t invitedOther, uint32_t maxPerTeam, uint32_t differential, uint32_t k = 1)
{
    if (!k || k > maxPerTeam || !ReleaseInvitesPlayer(invitedSide, invitedOther, differential, k))
        return 0;
    if (CoreInvitesEntry(invitedSide, invitedOther, maxPerTeam, differential, k))
        return 0;
    for (uint32_t releases = 1; releases <= k && releases <= invitedSide; ++releases)
    {
        if (CoreInvitesEntry(invitedSide - releases, invitedOther, maxPerTeam, differential, k))
            return releases;
    }
    return 0;
}

// Top-up: the player's side has fewer than k free slots and the other side is short, so there is no pair option, and
// releases alone would not get him invited either. One bot of the other faction queues and the core invites it alone
// (it evens the free slots); once the counts are close enough the release applies at a later interval.
inline bool TopUpNeeded(uint32_t invitedSide, uint32_t invitedOther, uint32_t maxPerTeam, uint32_t differential, uint32_t k = 1)
{
    return (k > maxPerTeam || invitedSide > maxPerTeam - k) && invitedOther < maxPerTeam &&
        !ReleaseInvitesPlayer(invitedSide, invitedOther, differential, k);
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

// Follow-up D (OB-20 16.10., AiPlayerbot.BG.BotOnlyCleanup): a bot-only fill battleground must not end through
// EndBattleGround - it hands out marks and experience even with WINNER_NONE, and only bots would get them. After the
// grace every manager bot of both sides leaves (no Deserter) until the battleground holds nobody and no open invite;
// BattleGround::Update then deletes it, before its PrematureFinish countdown is looked at. At most this many leaves per
// manager update.
constexpr uint32_t BotOnlyLeavesPerUpdate = 2;
// BotOnlyDrainSide: no side may leave now.
constexpr uint32_t NoDrainSide = 2;

// May a bot of this side leave now? Not when the core would then invite the smallest real-player entry waiting uninvited
// on this side into the freed slot (CoreInvitesEntry on the invited counts minus that bot), unless the bounded pause has
// run out. smallestWaiting 0 = no entry of this side waits.
inline bool BotOnlyLeaveAllowed(uint32_t invitedSide, uint32_t invitedOther, uint32_t maxPerTeam, uint32_t differential,
    uint32_t smallestWaiting, bool pauseOver)
{
    if (pauseOver || !smallestWaiting || !invitedSide)
        return true;
    return !CoreInvitesEntry(invitedSide - 1, invitedOther, maxPerTeam, differential, smallestWaiting);
}

// The side the next drain leave comes from: 0 alliance, 1 horde, NoDrainSide when no side has a bot that may leave. The
// core ends a battleground for its player counts only through the PrematureFinish countdown (a side below the minimum)
// and never at once for an empty side, so no order risks an immediate EndBattleGround. Of the sides that may leave, the
// one with more bots inside goes first (alliance on a tie): both sides shrink evenly, neither falls behind, and the
// core's balanced invite (InvitationType 1) has the least reason to pull a waiting player into the smaller side.
inline uint32_t BotOnlyDrainSide(uint32_t botsA, uint32_t botsH, bool mayLeaveA, bool mayLeaveH)
{
    bool const a = mayLeaveA && botsA;
    bool const h = mayLeaveH && botsH;
    if (a && h)
        return botsH > botsA ? 1 : 0;
    if (a)
        return 0;
    if (h)
        return 1;
    return NoDrainSide;
}

// twow-repo#517 stage c (AiPlayerbot.BG.Announce, owner contract and decision C0 16.10.): bots play battlegrounds by
// themselves, but only announced - 2/3/5 otherwise silent bots say one role-play line each in the World channel (or a
// server message goes out), DelaySeconds later both sides queue the template minimum. At most MaxBotBGPerBracket of
// these bot-own battlegrounds per level range, MaxConcurrent stays the global cap. Bots in a battleground without a real
// player get honor x BotOnlyHonorRate.

// AiPlayerbot.BG.BotOnlyHonorRate: the owner wants a reduction only. A rate <= 0 or above 1 (and NaN, where every
// comparison is false) counts as 1 - fail closed, honor unchanged.
inline float BotOnlyHonorRate(float cfgRate)
{
    return cfgRate > 0.0f && cfgRate <= 1.0f ? cfgRate : 1.0f;
}

// The listener does any work only while the clamped rate really reduces.
inline bool HonorRateActive(float cfgRate)
{
    return BotOnlyHonorRate(cfgRate) < 1.0f;
}

inline float ScaleBotOnlyHonor(float cp, float cfgRate)
{
    return cp * BotOnlyHonorRate(cfgRate);
}

// AiPlayerbot.BG.MaxBotBGPerBracket: 0 = this default (owner C0: one bot-own battleground per level range for a start).
constexpr uint32_t DefaultMaxBotBGPerBracket = 1;

inline uint32_t MaxOwnPerBand(uint32_t cfgMax)
{
    return cfgMax ? cfgMax : DefaultMaxBotBGPerBracket;
}

// Another bot-own battleground may form in a level range that holds ownInBand running or forming ones (any type).
inline bool OwnBandAllows(uint32_t ownInBand, uint32_t cfgMax)
{
    return ownInBand < MaxOwnPerBand(cfgMax);
}

// Level ranges a band index can name (levels up to 159); higher ones share the last.
constexpr uint32_t MaxLevelBands = 16;

// The level range (band) a battleground bracket covers, the same for every type: brackets count from each battleground's
// own minimum level (Player::GetBattleGroundBracketIdFromLevel - minLevel 0 keeps bracket 0 for level 1 alone and shifts
// the others by one), so WSG bracket 4 (50-59) and AV bracket 0 (51-60) are different indexes for overlapping levels. The
// band is the bracket's highest level, capped at the battleground's maximum level (0 = none), divided by ten: WSG 60-69
// and AB 60-69 with maximum 60 and AV 51-60 all are band 6, WSG/AB 50-59 band 5.
inline uint32_t LevelBand(uint32_t minLevel, uint32_t maxLevel, uint32_t bracket)
{
    uint32_t high;
    if (minLevel)
        high = minLevel + bracket * 10 + 9;
    else
        high = bracket ? bracket * 10 - 1 : 1;
    if (maxLevel && high > maxLevel)
        high = maxLevel;
    uint32_t const band = high / 10;
    return band < MaxLevelBands ? band : MaxLevelBands - 1;
}

// Announcers per announcement by battleground size (owner: "2-5 depending on battleground size"); 0 = no bot-own match
// of that type (BR is excluded, OB-00 D4; SV only after stage d).
inline uint32_t AnnouncerCount(uint32_t queueType)
{
    switch (queueType)
    {
        case QueueWS: return 2;
        case QueueAB: return 3;
        case QueueAV: return 5;
        default: return 0;
    }
}

// The due-queue of announcement lines holds at most this many entries (the largest AnnouncerCount): one announcement.
constexpr uint32_t MaxAnnounceDue = 5;

// AiPlayerbot.BG.Announce.IntervalMinutes: 0 = no bot-own battlegrounds; at most one day.
constexpr uint32_t MaxAnnounceIntervalMinutes = 1440;

inline uint32_t AnnounceIntervalMs(uint32_t cfgMinutes)
{
    uint32_t const minutes = cfgMinutes < MaxAnnounceIntervalMinutes ? cfgMinutes : MaxAnnounceIntervalMinutes;
    return minutes * 60 * 1000;
}

// One planner try per interval; interval 0 is never due (fail closed). last = 0 (never tried) is due at once; the
// unsigned difference keeps a wrapped millisecond clock correct.
inline bool PlannerDue(uint32_t now, uint32_t last, uint32_t interval)
{
    return interval && (!last || now - last >= interval);
}

// AiPlayerbot.BG.Announce.DelaySeconds: announcement to queue join, so real players can queue too. 0 = 60, minimum 20
// (every staggered line is out before the join), maximum MaxStageBSeconds.
constexpr uint32_t DefaultAnnounceDelaySeconds = 60;
constexpr uint32_t MinAnnounceDelaySeconds = 20;

inline uint32_t AnnounceDelayMs(uint32_t cfgSeconds)
{
    return ClampSeconds(cfgSeconds, DefaultAnnounceDelaySeconds, MinAnnounceDelaySeconds) * 1000;
}

// AiPlayerbot.BG.Announce.BotCooldownMinutes: one bot announces at most once per cooldown. 0 = 60, at most one day.
constexpr uint32_t DefaultAnnounceCooldownMinutes = 60;

inline uint32_t AnnounceCooldownMs(uint32_t cfgMinutes)
{
    uint32_t minutes = cfgMinutes ? cfgMinutes : DefaultAnnounceCooldownMinutes;
    if (minutes > MaxAnnounceIntervalMinutes)
        minutes = MaxAnnounceIntervalMinutes;
    return minutes * 60 * 1000;
}

// The announcers' lines are staggered 2-15 s after the announcement, all before the earliest join (MinAnnounceDelaySeconds).
constexpr uint32_t AnnounceStaggerMinMs = 2000;
constexpr uint32_t AnnounceStaggerMaxMs = 15000;

// AiPlayerbot.BG.Announce.Mode: 0 chat (default), 1 server message, 2 chat with the server message as fallback when no
// announcer qualifies. Any other value is chat.
constexpr uint32_t AnnounceModeChat = 0;
constexpr uint32_t AnnounceModeServer = 1;
constexpr uint32_t AnnounceModeFallback = 2;

inline uint32_t AnnounceMode(uint32_t cfgMode)
{
    return cfgMode <= AnnounceModeFallback ? cfgMode : AnnounceModeChat;
}

// Server message instead of chat: mode 1, or mode 2 with no qualifying announcer.
inline bool AnnounceUsesServer(uint32_t mode, uint32_t announcers)
{
    return mode == AnnounceModeServer || (mode == AnnounceModeFallback && !announcers);
}

// AiPlayerbot.BG.Announce.MaxPerHour: hard cap on announcement lines (chat lines and server lines) per hour window.
// 0 = no line at all (fail closed); then no bot-own battleground starts either.
constexpr uint32_t AnnounceHourMs = 3600 * 1000;

inline uint32_t AnnounceLinesAllowed(uint32_t usedThisHour, uint32_t maxPerHour, uint32_t wanted)
{
    if (!maxPerHour || usedThisHour >= maxPerHour)
        return 0;
    uint32_t const left = maxPerHour - usedThisHour;
    return wanted < left ? wanted : left;
}

// The hour window of MaxPerHour starts again (unsigned difference: a wrapped clock stays correct).
inline bool AnnounceHourOver(uint32_t now, uint32_t hourStart)
{
    return !hourStart || now - hourStart >= AnnounceHourMs;
}

// The type of the next bot-own battleground: of the possible ones (in the caller's fixed order), one never started
// first, else the one started longest ago - the last started type goes last. lastTypes holds the started types,
// oldest first. 0 = none possible.
inline uint32_t PickOwnType(std::vector<uint32_t> const& lastTypes, std::vector<uint32_t> const& possible)
{
    uint32_t best = 0;
    int64_t bestAt = 0;
    for (uint32_t type : possible)
    {
        int64_t at = -1;
        for (size_t i = 0; i < lastTypes.size(); ++i)
        {
            if (lastTypes[i] == type)
                at = int64_t(i);
        }
        if (!best || at < bestAt)
        {
            best = type;
            bestAt = at;
        }
    }
    return best;
}

// The planner's rotation remembers this many started types.
constexpr uint32_t OwnTypeHistory = 3;

// Free roster bots the planner's candidate loop visits at most per try (C2: a hard cap per interval).
constexpr uint32_t OwnCandidateVisitMax = 2000;

// C3: free candidates a side needs for a bot-own match - the template minimum plus that side's share of the announcers
// (the larger half), so one bot that got busy between the announcement and the queue join (combat, a taxi, ...) does
// not leave the side short and the players who answered the call without a match.
inline uint32_t OwnCandidatesNeeded(uint32_t minPerTeam, uint32_t queueType)
{
    return minPerTeam + (AnnouncerCount(queueType) + 1) / 2;
}

// The planner collects at most this many candidates per type, bracket and side: CandidateLimit(min), but never fewer
// than OwnCandidatesNeeded (a small template minimum with AV's five announcers).
inline uint32_t OwnCandidateLimit(uint32_t minPerTeam, uint32_t queueType)
{
    uint32_t const limit = CandidateLimit(minPerTeam);
    uint32_t const needed = OwnCandidatesNeeded(minPerTeam, queueType);
    return limit > needed ? limit : needed;
}

// Announcement text (D3): English, role-play, per faction. A pool is lines separated by '|', %bg is the full
// battleground name. A line outside printable ASCII, or longer than MaxAnnounceLineBytes with the longest name put in,
// is dropped at load; at most MaxAnnouncePoolLines per key.
constexpr uint32_t MaxAnnounceLineBytes = 255;
constexpr uint32_t MaxAnnouncePoolLines = 64;

inline char const* BgFullName(uint32_t queueType)
{
    switch (queueType)
    {
        case QueueWS: return "Warsong Gulch";
        case QueueAB: return "Arathi Basin";
        case QueueAV: return "Alterac Valley";
        default: return "";
    }
}

constexpr char const* LongestBgName = "Alterac Valley";

// Every %bg replaced by the name.
inline std::string ExpandAnnounceLine(std::string const& line, std::string const& bgName)
{
    std::string out;
    out.reserve(line.size() + bgName.size());
    for (size_t i = 0; i < line.size();)
    {
        if (line.compare(i, 3, "%bg") == 0)
        {
            out += bgName;
            i += 3;
        }
        else
            out += line[i++];
    }
    return out;
}

// Printable ASCII only (no control bytes, no UTF-8), not empty, and short enough with the longest name put in.
inline bool AnnounceLineValid(std::string const& line)
{
    if (line.empty())
        return false;
    for (char c : line)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E)
            return false;
    }
    return ExpandAnnounceLine(line, LongestBgName).size() <= MaxAnnounceLineBytes;
}

// '|'-separated pool to lines: blanks and tabs around a line trimmed, empty lines skipped, invalid ones (and those beyond
// MaxAnnouncePoolLines) dropped and counted.
inline std::vector<std::string> ParseAnnouncePool(std::string const& text, uint32_t& dropped)
{
    std::vector<std::string> lines;
    dropped = 0;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('|', start);
        if (end == std::string::npos)
            end = text.size();
        size_t first = start;
        size_t last = end;
        while (first < last && (text[first] == ' ' || text[first] == '\t'))
            ++first;
        while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t'))
            --last;
        if (last > first)
        {
            std::string const line = text.substr(first, last - first);
            if (!AnnounceLineValid(line) || lines.size() >= MaxAnnouncePoolLines)
                ++dropped;
            else
                lines.push_back(line);
        }
        start = end + 1;
    }
    return lines;
}

// The built-in pools (owner D3 style, his three examples included: the first generic line of each faction gives his
// WSG and AB lines for those battlegrounds, "Stormpike needs you, to Alterac!" is Alliance AV). team 0 alliance, 1 horde;
// queueType 0 = the generic pool, else that battleground's own lines.
inline std::string BuiltInAnnouncePool(uint32_t team, uint32_t queueType)
{
    if (!team)
    {
        switch (queueType)
        {
            case 0:
                return "For the Alliance! Join us in %bg!|"
                    "Brothers and sisters of the Alliance, %bg awaits. Who rides with me?|"
                    "The Horde will not hold %bg for long. To arms, Alliance!|"
                    "Stand with me in %bg, for Stormwind and Ironforge!|"
                    "Light guide our blades! We march on %bg.|"
                    "Alliance, sharpen your steel. %bg needs heroes!";
            case QueueWS:
                return "Silverwing Sentinels, the Warsong raiders are at our gates! To Warsong Gulch!|"
                    "Bring their flag home, Sentinels! To Warsong Gulch!";
            case QueueAB:
                return "The League of Arathor calls! Hold the farm, hold the mill, hold Arathi Basin!|"
                    "For Arathor! Take back Arathi Basin!";
            case QueueAV:
                return "Stormpike needs you, to Alterac!|"
                    "Dun Baldar stands! Who answers the Stormpike call in Alterac Valley?";
            default:
                return "";
        }
    }
    switch (queueType)
    {
        case 0:
            return "Lok'tar! The Horde gathers at %bg, who stands with us?|"
                "Blood and thunder! %bg calls, Horde!|"
                "For the Horde! The Alliance will fall in %bg.|"
                "Strength and honor! Who marches with me to %bg?|"
                "Throm-Ka, warriors! The Horde fights in %bg today.|"
                "Rally, Horde! %bg will be ours!";
        case QueueWS:
            return "Warsong Outriders, the night elves guard their flag in Warsong Gulch. Take it!|"
                "Grom's warriors, to Warsong Gulch! Bring me their flag!";
        case QueueAB:
            return "The Defilers march on Arathi Basin! Who joins the hunt?|"
                "Defilers, Arathi Basin is ripe for the taking!";
        case QueueAV:
            return "Frostwolf clan, to arms! Alterac Valley is ours!|"
                "For Drek'Thar! The Frostwolves ride to Alterac Valley!";
        default:
            return "";
    }
}

// count distinct line indexes out of poolSize (fewer when the pool is smaller): a partial Fisher-Yates shuffle driven by
// the caller's random rolls (a missing roll counts as 0), so the same line is never said twice in one announcement.
inline std::vector<uint32_t> PickLineIndexes(uint32_t poolSize, uint32_t count, std::vector<uint32_t> const& rolls)
{
    std::vector<uint32_t> idx(poolSize);
    for (uint32_t i = 0; i < poolSize; ++i)
        idx[i] = i;
    uint32_t const n = count < poolSize ? count : poolSize;
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t const roll = i < rolls.size() ? rolls[i] : 0;
        uint32_t const j = i + roll % (poolSize - i);
        uint32_t const tmp = idx[i];
        idx[i] = idx[j];
        idx[j] = tmp;
    }
    idx.resize(n);
    return idx;
}

// Announcer i of an announcement: alternating sides, starting with firstTeam.
inline uint32_t AnnouncerTeam(uint32_t i, uint32_t firstTeam)
{
    return (firstTeam + i) % 2;
}
}
