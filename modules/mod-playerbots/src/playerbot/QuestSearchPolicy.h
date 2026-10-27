#pragma once

#include <algorithm>
#include <atomic>
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

// twow-repo#601 quest acceptance step 1b (OB-00 27.10.2026, cli-quest-organic/quest-acceptance/STEP2.md): in AB-2 not one normal
// quest giver was routed to. Even uncontested, 90 % of the giver fetches found no active giver, and every giver point that came
// back failed IsActive as no_mark (the NPC offers this bot nothing). Two log-only splits answer why:
//
// (1) no_mark: the first check of Player::CanTakeQuest (in its order) that the giver point's own quest fails for this bot. Pass
//     = true. A quest that passes every check is split by its own dialog status, because the NPC mark is the highest status over
//     all of the NPC's relations (review 27.10.): masked = available, but a higher status (e.g. a completed or parked turn-in at
//     the same NPC) hides it; repeatable = already rewarded repeatable or auto-complete; low_level_masked = a grey quest hidden by
//     an incomplete one; anything else is "other". NoMarkOf returns other for all of them; the caller adds the split.
enum NoMarkReason : uint32_t { NoMarkMaxLevel, NoMarkDone, NoMarkInLog, NoMarkExclusive, NoMarkClass, NoMarkRace, NoMarkLevel,
    NoMarkSkill, NoMarkCondition, NoMarkReputation, NoMarkPrevQuest, NoMarkTimed, NoMarkChain, NoMarkInactiveQuest, NoMarkChallenge,
    NoMarkMasked, NoMarkRepeatable, NoMarkLowLevelMasked, NoMarkOther, NoMarkReasons };

struct QuestTakeChecks
{
    bool maxLevel = true;      // GetMaxLevel() == 0 or >= the bot's level
    bool notRewarded = true;   // GetQuestRewardStatus false: a failed status check is then the quest in the log
    bool status = true;        // SatisfyQuestStatus
    bool exclusive = true;
    bool cls = true;
    bool race = true;
    bool level = true;
    bool skill = true;
    bool condition = true;
    bool reputation = true;
    bool prevQuest = true;
    bool timed = true;
    bool chain = true;         // SatisfyQuestNextChain && SatisfyQuestPrevChain
    bool active = true;        // Quest::IsActive
    bool challenge = true;
};

inline NoMarkReason NoMarkOf(QuestTakeChecks const& c)
{
    if (!c.maxLevel) return NoMarkMaxLevel;
    if (!c.status)
        return !c.notRewarded ? NoMarkDone : NoMarkInLog;   // SatisfyQuestStatus: rewarded (not repeatable) or in the log
    if (!c.exclusive) return NoMarkExclusive;
    if (!c.cls) return NoMarkClass;
    if (!c.race) return NoMarkRace;
    if (!c.level) return NoMarkLevel;
    if (!c.skill) return NoMarkSkill;
    if (!c.condition) return NoMarkCondition;
    if (!c.reputation) return NoMarkReputation;
    if (!c.prevQuest) return NoMarkPrevQuest;
    if (!c.timed) return NoMarkTimed;
    if (!c.chain) return NoMarkChain;
    if (!c.active) return NoMarkInactiveQuest;
    if (!c.challenge) return NoMarkChallenge;
    return NoMarkOther;
}

inline char const* NoMarkName(NoMarkReason reason)
{
    static char const* const names[NoMarkReasons] = { "max_level", "done", "in_log", "exclusive", "class", "race", "level", "skill",
        "condition", "reputation", "prev_quest", "timed", "chain", "inactive_quest", "challenge", "masked", "repeatable",
        "low_level_masked", "other" };
    return reason < NoMarkReasons ? names[reason] : "other";
}

// (2) The givers QuestRelationTravelDestination::IsPossible drops silently inside the async giver fetch (GetPartitions with
//     onlyPossible), by its rule, in its order; kept = passed. Only givers within the fetch range are judged (out_of_range = the
//     others, TravelMgr::GetDestinations takes the range first when traced). One counter set per traced request, written by the
//     fetch job only.
enum GiverDrop : uint32_t { GiverDropStrategy, GiverDropFocus, GiverDropQuestLevel, GiverDropMinMaxLevel, GiverDropBandGrey,
    GiverDropCondition, GiverDropFreeSlots, GiverDropElite, GiverDrops };

struct GiverDropCounts
{
    uint32_t dropped[GiverDrops] = {};
    uint32_t kept = 0;
    uint32_t outOfRange = 0;

    void Drop(GiverDrop reason)
    {
        if (reason < GiverDrops)
            ++dropped[reason];
    }

    // "giver_drops=strategy:0,focus:0,quest_level:3,min_max:1,band_grey:0,condition:0,free_slots:12,elite:0 giver_kept=4
    // giver_out_of_range=310"
    std::string Format() const
    {
        static char const* const names[GiverDrops] = { "strategy", "focus", "quest_level", "min_max", "band_grey", "condition",
            "free_slots", "elite" };
        std::string out = "giver_drops=";
        for (uint32_t i = 0; i < GiverDrops; ++i)
            out += std::string(i ? "," : "") + names[i] + ":" + std::to_string(dropped[i]);
        return out + " giver_kept=" + std::to_string(kept) + " giver_out_of_range=" + std::to_string(outOfRange);
    }
};

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
    // twow-repo#581 (QL-1): objectives of a quest this bot kept failing at.
    uint32_t questLoopSuppressed = 0;
    // twow-repo#587 (Z3): battleground quests skipped as quest-first targets.
    uint32_t battlegroundQuest = 0;
    // twow-repo#601 (QuestLoop.InactiveReselect): objectives whose camp this bot just found empty. In the line only when > 0, so the
    // line of every bot without the switch stays as it was.
    uint32_t inactiveReselect = 0;
    // twow-repo#594 / P3-0 (AiPlayerbot.DirectTargets.TraceInactive): why IsActive turned a candidate down. A split of
    // notActive only - notActive itself is counted as before (Z1b and the #485 park sum it). All 0 without the switch.
    enum InactiveReason { InactiveImpossible, InactiveMap, InactiveNoMark, InactiveLowLevelNoReward, InactiveNoTurnIn,
        InactiveHostile, InactiveOther, InactiveReasons };
    uint32_t notActiveReason[InactiveReasons] = {};
    // twow-repo#601 quest acceptance step 1b: the no_mark count split by NoMarkOf (with TraceTravelDecisions too). A split of
    // notActiveReason[InactiveNoMark] only; in the line only when counted.
    uint32_t noMarkWhy[NoMarkReasons] = {};

    std::string FormatInactiveReasons() const
    {
        static char const* const names[InactiveReasons] = { "impossible", "map", "no_mark", "low_level_no_reward", "no_turnin",
            "hostile", "other" };
        uint32_t sum = 0;
        for (uint32_t count : notActiveReason)
            sum += count;
        if (!sum)
            return std::string();
        std::string out = " not_active_reasons=";
        for (uint32_t i = 0; i < InactiveReasons; ++i)
            out += std::string(i ? "," : "") + names[i] + ":" + std::to_string(notActiveReason[i]);
        uint32_t noMarkSum = 0;
        for (uint32_t count : noMarkWhy)
            noMarkSum += count;
        if (noMarkSum)
        {
            out += " no_mark_why=";
            bool first = true;
            for (uint32_t i = 0; i < NoMarkReasons; ++i)
                if (noMarkWhy[i])
                {
                    out += std::string(first ? "" : ",") + NoMarkName(NoMarkReason(i)) + ":" + std::to_string(noMarkWhy[i]);
                    first = false;
                }
        }
        return out;
    }

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
            " resume_skipped=" + std::to_string(resumeSkipped) +
            " quest_loop_suppressed=" + std::to_string(questLoopSuppressed) +
            " battleground_quest=" + std::to_string(battlegroundQuest) +
            (inactiveReselect ? " inactive_reselect=" + std::to_string(inactiveReselect) : std::string()) + FormatInactiveReasons();
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
    float const own = (std::max)(2000.f, 400.f + float(level) * 10.f);
    if (stage >= 2)
        return (std::max)(own, 12000.f);
    if (stage == 1)
        return (std::max)(own, 6000.f);
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

// twow-repo#601 / #614 (L1-b, 19.10.2026): grey as the core colours it - Formulas.h GetColorCode: GRAY = mob level <=
// MaNGOS::XP::GetGrayLevel(player level). MaNGOS::XP::Gain is no grey test before a kill: outside dungeons it multiplies by
// the damage-origin share (Creature::GetXPModifierDueToDamageOrigin), which is 0 for every mob nobody has damaged yet - so
// every untouched mob read as "no XP" (L1-b: 364,754 "not xp" skips against 1,376 grind picks in 6 h).
inline bool GreyByLevel(uint32_t mobLevel, uint32_t grayLevel)
{
    return mobLevel <= grayLevel;
}

// The "no XP" test of the grind skip with AiPlayerbot.Grind.GreyByLevel (CLI-Deep review 19.10.2026): grey by level, or a
// mob that never rewards a kill (UNIT_STAT_NO_KILL_REWARD) or whose template gives no XP (xp_multiplier 0) - the reasons
// for which XP::Gain is rightly 0 whatever the damage origin.
inline bool NoXpTarget(bool greyByLevel, bool noKillReward, bool zeroXpMultiplier)
{
    return greyByLevel || noKillReward || zeroXpMultiplier;
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
// twow-repo#594 / P3-a (AiPlayerbot.DirectTargets.FreshBackoffSeconds): a fresh bot (FreshBot below) waits freshSeconds
// instead - live 14.10. L1-3 bots stood a median 10.3 min between a rejected quest route and the next request. The
// failure count still grows (Z1b, the grind fallback and the logs read it); freshSeconds 0 = the 8.1 rule for everyone.
inline uint32_t RouteBackoffSeconds(uint32_t failures, bool fresh = false, uint32_t freshSeconds = 0)
{
    if (fresh && freshSeconds)
        return freshSeconds;
    uint32_t seconds = RouteBackoffBaseSeconds;
    for (uint32_t i = 1; i < failures && seconds < RouteBackoffMaxSeconds; ++i)
        seconds *= 2;
    return seconds < RouteBackoffMaxSeconds ? seconds : RouteBackoffMaxSeconds;
}

// Hotfix 8.32 (twow-repo#544): a quest-first roster bot whose quest route search found nothing
// MinFailures times in a row (backoff 2+4 min) may travel to a grind spot instead of idling
// (v35: ~13 % of bot time "no destination", the bots that grind progress ~5x faster). 0 = off.
// twow-repo#601 (QuestFirstProgression.GrindFallbackMinutes): the grind fallback is "fresh" only that many minutes after
// the last empty quest route search (lastFailAt); minutes 0 or no stamp = as before (the fallback holds until a route is
// found, which never happens while grind 6.35 outranks the quest request 6.3 - 37,024 grind fallbacks against 2,012
// quest route requests of L10+ bots in the 17.10. soak).
inline bool GrindFallbackFresh(uint32_t minutes, uint32_t lastFailAt, uint32_t now)
{
    return !minutes || !lastFailAt || now < lastFailAt || now - lastFailAt < minutes * 60;
}

inline bool GrindFallback(bool questFirst, uint32_t routeFailures, uint32_t minFailures)
{
    return questFirst && minFailures && routeFailures >= minFailures;
}

// twow-repo#594 / P3-a (AiPlayerbot.DirectTargets.Enable): a "fresh" bot is a quest-first roster bot at or below
// FreshMaxLevel - the new L1-3 bots that stood after a rejected first quest route. maxLevel 0 = nobody.
inline bool FreshBot(bool directTargets, uint32_t level, uint32_t maxLevel)
{
    return directTargets && maxLevel && level <= maxLevel;
}

// [DirectTarget] state=summary once a minute: rejected quest routes of fresh bots, how many got the short backoff, how
// many layers stood back for a Z1b rescue, grind fallbacks a fresh bot got through its own threshold, and the IsActive
// reasons of P3-0 summed over all rejections.
struct DirectTargetCounters
{
    std::atomic<uint32_t> freshRejections{ 0 };
    std::atomic<uint32_t> freshBackoffs{ 0 };
    std::atomic<uint32_t> rescueSkips{ 0 };
    std::atomic<uint32_t> freshGrind{ 0 };
    std::atomic<uint32_t> inactive[RejectCounts::InactiveReasons] = {};
};

inline DirectTargetCounters& DirectTargetStats()
{
    static DirectTargetCounters counters;
    return counters;
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

    // twow-repo#587 (QuestRescue.KeepRequestWhenFull): is a slot free now, without taking it.
    bool HasCapacity(uint32_t now)
    {
        while (!recent.empty() && now - recent.front() >= RescueGlobalWindowSeconds)
            recent.pop_front();
        return recent.size() < RescueGlobalMax;
    }

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
