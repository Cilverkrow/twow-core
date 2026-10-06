#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ai::quest_search
{
// Hotfix 8.26: a roster bot on its own that the quest rescue moves binds its hearthstone there,
// so a later hearthstone does not undo the rescue. Only bots without a real player; any map
// (the rescue target is always of the bot's own faction).
inline bool RebindAtRescue(bool rosterBot, bool realMaster, uint32_t /*homebindMap*/, uint32_t /*targetMap*/)
{
    return rosterBot && !realMaster;
}

// twow-repo#421 (train 8), owner decision 2026-09-29: a roster bot without a
// quest target does not fall back to grinding - it searches for quests, and as
// a last resort, after ~20 min without progress, it is teleported to another
// race's starting area of its faction that fits its level.

// ---- A) why the route choice found nothing ------------------------------

// Per-reason counts of the candidates SetBestTarget turned down; written with
// [QuestFirstRoute] state=rejected. Counting only, inside the existing loop.
struct RejectCounts
{
    uint32_t turnInSuppressed = 0;
    uint32_t deathSuppressed = 0;
    uint32_t otherMapGather = 0;
    uint32_t knownInactive = 0;
    uint32_t hubFilter = 0;
    uint32_t notActive = 0;
    uint32_t enemyZone = 0;
    uint32_t crossMap = 0;
    uint32_t zoneLevel = 0;
    uint32_t dangerMap = 0;
    uint32_t rangeSkip = 0;
    uint32_t movedAway = 0;
    // twow-repo#485: candidates a choice that ran out of time (#416) had checked
    // and this one skipped - not judged by this choice.
    uint32_t resumeSkipped = 0;

    std::string Format() const
    {
        return "turnin_suppressed=" + std::to_string(turnInSuppressed) +
            " death_suppressed=" + std::to_string(deathSuppressed) +
            " other_map_gather=" + std::to_string(otherMapGather) +
            " known_inactive=" + std::to_string(knownInactive) +
            " hub_filter=" + std::to_string(hubFilter) +
            " not_active=" + std::to_string(notActive) +
            " enemy_zone=" + std::to_string(enemyZone) +
            " cross_map=" + std::to_string(crossMap) +
            " zone_level=" + std::to_string(zoneLevel) +
            " danger_map=" + std::to_string(dangerMap) +
            " range_skip=" + std::to_string(rangeSkip) +
            " moved_away=" + std::to_string(movedAway) +
            " resume_skipped=" + std::to_string(resumeSkipped);
    }
};

// ---- B) widening quest search ---------------------------------------------

constexpr uint32_t MaxStage = 2;
// A bot widens at most once per this many seconds.
constexpr uint32_t WidenIntervalSeconds = 120;

// Quest-giver search radius (yards). Stage 0 is today's radius (own zone,
// #307 floor of 2000); stage 1 reaches neighbouring zones, stage 2 the region.
// Same map only - the fetch never leaves the continent; the zone-level filter
// of #307 still keeps a bot out of zones clearly above its level.
inline float GiverRadius(uint32_t level, uint32_t stage)
{
    float const own = std::max(2000.f, 400.f + float(level) * 10.f);
    if (stage >= 2)
        return std::max(own, 12000.f);
    if (stage == 1)
        return std::max(own, 6000.f);
    return own;
}

inline uint32_t NextStage(uint32_t stage, uint32_t lastWiden, uint32_t now)
{
    if (stage >= MaxStage || now - lastWiden < WidenIntervalSeconds)
        return stage;
    return stage + 1;
}

// Owner addition (#421, Deygo L14 grinding level-1 wolves in Dun Morogh): a
// zone more than 3 levels below the bot is no sensible place - the quest search
// then starts at the neighbouring zones (stage 1) right away.
inline bool ZoneBelowBot(int32_t zoneLevel, uint32_t level)
{
    return zoneLevel > 0 && zoneLevel + 3 < int32_t(level);
}

// Grey creatures (no XP) are no grind target for a roster bot on its own,
// unless a quest needs them or they attack the bot.
inline bool SkipGreyTarget(bool rosterOnItsOwn, bool grey, bool neededForQuest, bool attacksBot)
{
    return rosterOnItsOwn && grey && !neededForQuest && !attacksBot;
}

// Grey engagements per bot and hour - the measuring point for the rule above.
constexpr uint32_t GreyReportSeconds = 3600;
struct GreyEngagements
{
    uint32_t count = 0;
    uint32_t windowStart = 0;

    void Add(uint32_t now)
    {
        if (!windowStart)
            windowStart = now;
        ++count;
    }

    // True when a window with engagements is over; the caller logs `count`
    // and then calls Reset.
    bool Due(uint32_t now) const
    {
        return count && now - windowStart >= GreyReportSeconds;
    }

    void Reset()
    {
        count = 0;
        windowStart = 0;
    }
};

// Hub preference while widened: a quest giver counts the other givers within
// HubRadius; more is better. Skipped for long lists (cost bound, the sort runs
// in the destination job, not on the map thread).
constexpr float HubRadius = 150.f;
constexpr size_t HubSortMaxPoints = 400;

// ---- C) rescue teleport after ~30 min without progress ----------------------

// Default of AiPlayerbot.QuestRescue.IdleMinutes (owner decision 29.09: 30 min).
constexpr uint32_t RescueIdleSeconds = 30 * 60;
constexpr uint32_t RescuePerBotSeconds = 2 * 60 * 60;
constexpr uint32_t RescueGlobalWindowSeconds = 10 * 60;
constexpr uint32_t RescueGlobalMax = 5;
constexpr uint32_t ProgressCheckSeconds = 60;
// Up to this level the target is another race's starting area.
constexpr uint32_t StartAreaMaxLevel = 10;
// Inn targets closer than this to the bot do not count as a change of area.
constexpr float RescueMinDistance = 1000.f;

// Progress = any change of level, XP or quest state (accept, objective,
// completion, hand-in), sampled once per minute.
struct ProgressTracker
{
    uint64_t snapshot = 0;
    uint32_t lastChange = 0;

    // Returns true when the snapshot changed (progress) or on the first call.
    bool Update(uint64_t newSnapshot, uint32_t now)
    {
        if (lastChange && newSnapshot == snapshot)
            return false;
        snapshot = newSnapshot;
        lastChange = now;
        return true;
    }

    uint32_t IdleSeconds(uint32_t now) const
    {
        return lastChange && now > lastChange ? now - lastChange : 0;
    }
};

// Hotfix 8.1 (#421 train 8 acceptance): nine of ten test bots stood still from their
// login on - they asked for one quest route and never again, so the search never reached
// MaxStage and RescueDue never fired. Twice the idle limit rescues regardless of the stage.
inline bool HardIdleRescueDue(uint32_t idleSeconds, uint32_t lastRescue, uint32_t now,
    uint32_t idleLimitSeconds = RescueIdleSeconds)
{
    return idleSeconds >= 2 * idleLimitSeconds && (!lastRescue || now - lastRescue >= RescuePerBotSeconds);
}

// Hotfix 8.1: [Idle] after IdleLogSeconds without progress, then at most every IdleRepeatSeconds.
constexpr uint32_t IdleLogSeconds = 10 * 60;
constexpr uint32_t IdleRepeatSeconds = 30 * 60;
inline bool IdleLogDue(uint32_t idleSeconds, uint32_t lastLog, uint32_t now)
{
    return idleSeconds >= IdleLogSeconds && (!lastLog || now - lastLog >= IdleRepeatSeconds);
}

// Hotfix 8.1: a quest route request that found nothing (no fallback) is not repeated at
// once - two stuck bots sent 237 of 370 such requests in 50 minutes. 2, 4, 8, then 10 min.
constexpr uint32_t RouteBackoffBaseSeconds = 120;
constexpr uint32_t RouteBackoffMaxSeconds = 600;
inline uint32_t RouteBackoffSeconds(uint32_t failures)
{
    uint32_t seconds = RouteBackoffBaseSeconds;
    for (uint32_t i = 1; i < failures && seconds < RouteBackoffMaxSeconds; ++i)
        seconds *= 2;
    return seconds < RouteBackoffMaxSeconds ? seconds : RouteBackoffMaxSeconds;
}

// Hotfix 8.32 (twow-repo#544): a quest-first roster bot whose quest route search found nothing
// MinFailures times in a row (backoff 2+4 min) may travel to a grind spot instead of idling
// (v35: ~13 % of bot time "no destination", the bots that grind progress ~5x faster). 0 = off.
inline bool GrindFallback(bool questFirst, uint32_t routeFailures, uint32_t minFailures)
{
    return questFirst && minFailures && routeFailures >= minFailures;
}

// Hotfix 8.1 [MemStores]: a bot whose UpdateAI did not run for this long counts as stale.
constexpr uint32_t StaleUpdateSeconds = 10 * 60;
inline bool UpdateStale(uint32_t lastUpdate, uint32_t now)
{
    return lastUpdate && now > lastUpdate && now - lastUpdate >= StaleUpdateSeconds;
}

inline bool RescueDue(uint32_t idleSeconds, uint32_t stage, uint32_t lastRescue, uint32_t now,
    uint32_t idleLimitSeconds = RescueIdleSeconds)
{
    return idleSeconds >= idleLimitSeconds && stage >= MaxStage &&
        (!lastRescue || now - lastRescue >= RescuePerBotSeconds);
}

// At most RescueGlobalMax rescues per RescueGlobalWindowSeconds, world-wide.
struct RescueLimiter
{
    std::deque<uint32_t> recent;

    bool TryAcquire(uint32_t now)
    {
        while (!recent.empty() && now - recent.front() >= RescueGlobalWindowSeconds)
            recent.pop_front();
        if (recent.size() >= RescueGlobalMax)
            return false;
        recent.push_back(now);
        return true;
    }
};

// Levels 11-20 of the Turtle races have no inn cache of their own region. OB-50
// researched the follow-up hubs and the owner decided (#421, 29.09): high elf 10
// -> Darkshore / Auberdine (quest 41259 "Journey to Auberdine"), goblin 9 ->
// Ratchet (The Barrens). The target is the spawn point of these NPCs
// (innkeepers / quest givers), looked up once in the creature data.
// Hotfix 8.29 (twow-repo#532, owner 04.10.2026): goblins leave Blackstone Island (zone 5536) only by
// "Gazzik's Flying Machine" (creature 50597, gossip script npc_flying_machine -> taxi path 311 to
// Sparkwater Port; 50598 flies back on path 322). Neither travel nor follow knew it: every goblin
// waited 30-60 minutes for the quest rescue, which sent it to a random horde start area.
namespace goblin_island
{
constexpr uint32_t IslandZone = 5536;
constexpr uint32_t IslandPath = 311;          // Blackstone Island -> Sparkwater Port
constexpr uint32_t ReturnPath = 322;          // Sparkwater Port -> Blackstone Island
constexpr float IslandMachineX = -571.0f, IslandMachineY = -7850.0f, IslandMachineZ = 52.0f;
constexpr float PortMachineX = 819.0f, PortMachineY = -5006.0f, PortMachineZ = 20.0f;
constexpr float RazorHillX = 340.0f, RazorHillY = -4686.0f, RazorHillZ = 17.0f;
constexpr uint32_t LeaveLevel = 10;
constexpr uint32_t LeaveIdleSeconds = 15 * 60;
constexpr float BoardDistance = 8.0f;          // close enough to "use" the machine
constexpr float FollowDistance = 30.0f;        // a follower this close to a machine flies along

// Hotfix 8.29a (v36 smoke: over100 145/min vs 25): the island check runs for a goblin on map 1
// inside the island's box (position only, no terrain lookup); every other bot skips it.
constexpr uint32_t GoblinRace = 9;
constexpr float BoxMinX = -1500.0f, BoxMaxX = 600.0f, BoxMinY = -8600.0f, BoxMaxY = -6600.0f;
inline bool MayBeOnIsland(uint32_t race, uint32_t mapId, float x, float y)
{
    return race == GoblinRace && mapId == 1 && x >= BoxMinX && x <= BoxMaxX && y >= BoxMinY && y <= BoxMaxY;
}

// A roster bot on its own leaves the island from LeaveLevel on, or earlier when it has made no
// progress for LeaveIdleSeconds (no quest left there).
inline bool ShouldLeaveIsland(uint32_t zone, uint32_t level, uint32_t idleSeconds)
{
    return zone == IslandZone && (level >= LeaveLevel || idleSeconds >= LeaveIdleSeconds);
}

// The flight a follower takes when its real master flies off from a machine: by the machine
// the follower stands at (0 = none in reach).
inline uint32_t FollowPath(float distToIslandMachine, float distToPortMachine)
{
    if (distToIslandMachine <= FollowDistance)
        return IslandPath;
    if (distToPortMachine <= FollowDistance)
        return ReturnPath;
    return 0;
}

// The quest rescue sends a goblin below level 11 to Durotar (Sparkwater Port, Razor Hill) instead
// of another race's start area.
inline bool UsesGoblinStartRescue(uint32_t race, uint32_t level)
{
    return race == 9 && level <= 10;
}
}

inline bool UsesRescueAnchors(uint32_t race, uint32_t level)
{
    return level > StartAreaMaxLevel && level <= 20 && (race == 9 || race == 10);
}

inline std::vector<uint32_t> RescueAnchorEntries(uint32_t race)
{
    if (race == 9)                              // goblin
        return { 6791 };                        // Innkeeper Wiley (Ratchet) - owner decision 29.09
    if (race == 10)                             // high elf
        return { 6737, 10219 };                 // Shaussiy, Gwennyth Bly'Leggonde (Auberdine)
    return {};
}

// Hotfix 8.5 (twow-repo#329, Latchigedap L15 in Mulgore for 8 h): a gathering travel
// purpose (relevance 6.5) always beat the quest request (6.3). With a skinning target
// active, "request quest travel target" stayed USELESS and eight finished quests were
// never handed in - 164 of the roster held 586 such quests (dump 30.09). A roster bot
// on its own hands in first and gathers afterwards.
inline bool GatherYieldsToTurnIn(bool rosterOnItsOwn, uint32_t finishedQuests)
{
    return rosterOnItsOwn && finishedQuests > 0;
}

// Hotfix 8.7 (pin v22 acceptance): the server runs with Quests.LowLevelHideDiff = -1, so
// the hide-diff rule called nothing grey and 8.5 dropped no grey quest (Latchigedap kept
// 746/771 at level 15). Grey is the XP grey level, as for grind targets
// (MaNGOS::XP::GetGrayLevel): a quest at or below it is grey.
inline bool IsGreyQuest(uint32_t questLevel, uint32_t grayLevel)
{
    return questLevel <= grayLevel;
}

// Hotfix 8.5: an open grey quest made its creatures "needed for quest" and so let the
// bot grind grey mobs past the #421 filter (Mazzranache, level 8, at level 15: 50-125
// grey targets an hour). CleanQuestLogAction dropped grey quests only above 14 quests.
// A roster bot on its own now drops them at once - finished ones are still handed in,
// class quests stay.
inline bool DropGreyQuest(bool rosterOnItsOwn, uint32_t questLevel, uint32_t grayLevel,
    bool complete, bool classQuest)
{
    return rosterOnItsOwn && !complete && !classQuest && IsGreyQuest(questLevel, grayLevel);
}
}
