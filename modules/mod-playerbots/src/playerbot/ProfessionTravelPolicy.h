#pragma once

#include <cstdint>

namespace ai::profession_travel
{
// twow-repo#333 Berufe phase 1 (CLI-BERUFE PLAN-B1 25.10.2026, OB-00 go; QFO slot agreed with CLI-Deep): a persistent
// roster bot learned its planned gathering profession only when ordinary travel happened to bring it within 120 yd
// of a matching trainer (#306), and no bot got a mining pick or skinning knife (the factory gives tools only with the
// skill already there, which it no longer grants). Two named travel targets close that gap, both only as fillers of
// the quest-first gap, never ahead of a quest:
//   "trainer profession" (AiPlayerbot.ProfessionTraining.Travel) - a trainer that teaches the bot a GREEN rank of a
//       phase-one skill now (first rank or the next rank at the skill cap; the core decides GREEN);
//   "tool vendor" (AiPlayerbot.ProfessionUse.ToolBuy) - a vendor that sells the missing pick or knife; "rpg buy" buys
//       it there (ItemUsage SKILL). With the same switch a gather purpose for mining/skinning needs the tool, so a bot
//       without a pick no longer travels to an ore vein to fail there for 15 minutes.
// Pure rules, no game types, no locks, no config reads. Switch modes: ai::cohort (0 off, 1 all, 2 cohort B).

constexpr std::uint32_t kHerbalism = 182;
constexpr std::uint32_t kMining = 186;
constexpr std::uint32_t kSkinning = 393;
constexpr std::uint32_t kFirstAid = 129;

constexpr std::uint32_t kMiningPick = 2901;
constexpr std::uint32_t kSkinningKnife = 7005;

// Apprentice ranks are taught from level 5 (RECHERCHE OB-50 2.).
constexpr std::uint32_t kMinLevel = 5;
// A started trip holds its travel condition until the backoff of its start ends plus kHoldAfterBackoffSeconds, never
// longer than kTripHoldSeconds (CLI-Deep 25.10.: the backoff is 2-10 min; a longer hold keeps the bot out of the quest
// flow - TravelActionMultiplier holds the quest request back while the trip runs).
constexpr std::uint32_t kTripHoldSeconds = 1800;
constexpr std::uint32_t kHoldAfterBackoffSeconds = 600;
// One trip of a kind per bot and hour: a visit that taught nothing is not repeated at once.
constexpr std::uint32_t kTripCooldownSeconds = 3600;

// Phase 1 trains the gathering professions and first aid only (no cooking, fishing, survival trips).
inline bool IsPhaseOneSkill(std::uint32_t skill)
{
    return skill == kHerbalism || skill == kMining || skill == kSkinning || skill == kFirstAid;
}

// The quest-first gap (CLI-Deep 25.10.2026): the 8.1 quest route backoff runs (the last quest search found nothing),
// no open turn-in waits and no F4 band walk / band trip runs. The quest request (6.3) cannot act then.
inline bool InQuestGap(std::uint32_t now, std::uint32_t backoffUntil, bool turnInWaiting, std::uint32_t bandTripEnd)
{
    return now < backoffUntil && !turnInWaiting && bandTripEnd == 0;
}

// The hold end of a trip requested at `now` while the backoff runs until `backoffUntil`.
inline std::uint32_t HoldUntil(std::uint32_t now, std::uint32_t backoffUntil)
{
    std::uint32_t const cap = now + kTripHoldSeconds;
    std::uint32_t const afterBackoff = backoffUntil + kHoldAfterBackoffSeconds;
    return afterBackoff < cap ? afterBackoff : cap;
}

// holdUntil = HoldUntil() of the last request of this kind (0 = never).
inline bool TripHolds(std::uint32_t now, std::uint32_t holdUntil)
{
    return holdUntil != 0 && now < holdUntil;
}

// The seconds a trip ran on after the backoff of its start ended: its quest-time cost (CLI-Deep's hold_over_s).
inline std::uint32_t HoldOverSeconds(std::uint32_t now, std::uint32_t backoffUntil)
{
    return now > backoffUntil ? now - backoffUntil : 0;
}

// Why a started trip stops holding (the [ProfTravel] state=end reason): its reason is gone (rank learned / tool in
// the bags), a turn-in opened, or the hold ran out.
inline char const* EndReason(bool reasonLasts, bool turnInWaiting, bool holds)
{
    if (!reasonLasts)
        return "done";
    if (turnInWaiting)
        return "turn_in";
    if (!holds)
        return "cap";
    return nullptr;
}

// "manual int" suffixes of a trip kind, appended to kTrainerTripValue / kToolTripValue.
constexpr char const* kHoldUntilSuffix = " hold until";
constexpr char const* kBackoffEndSuffix = " backoff end";
constexpr char const* kOpenSuffix = " open";

inline bool CooldownOver(std::uint32_t now, std::uint32_t startedAt)
{
    return startedAt == 0 || now >= startedAt + kTripCooldownSeconds;
}

// "manual int" values: the time of the last trip request per kind (0 = never).
constexpr char const* kTrainerTripValue = "profession trainer trip at";
constexpr char const* kToolTripValue = "profession tool trip at";

// A new trip, the cheap part (switch applies, roster bot on its own, level, cooldown); the caller then asks InQuestGap.
inline bool MayStartTrip(bool applies, bool rosterOnItsOwn, std::uint32_t level, bool cooldownOver)
{
    return applies && rosterOnItsOwn && level >= kMinLevel && cooldownOver;
}

// The tool a learned gathering skill needs and the bags lack; 0 = none missing. Mining first.
inline std::uint32_t MissingTool(bool mining, bool hasPick, bool skinning, bool hasKnife)
{
    if (mining && !hasPick)
        return kMiningPick;
    if (skinning && !hasKnife)
        return kSkinningKnife;
    return 0;
}

// A gather purpose for this skill is useful only with its tool (the loot check LootObjectStack.cpp needs it anyway).
inline bool HasGatherTool(std::uint32_t skill, bool hasPick, bool hasKnife)
{
    if (skill == kMining)
        return hasPick;
    if (skill == kSkinning)
        return hasKnife;
    return true;
}

// P1-b2 (CLI-PATH 25.10.2026, path/tool-vendors-reachability.tsv + trainers-reachability.tsv): GetPartitions checks neither
// the travel graph nor the faction, so a trip went to a vendor or trainer the bot cannot reach (a zone without travel nodes:
// Blackstone Island, Alah'Thalas, Moonwhisper Coast, Tel'Abim ... - the nearest node 516-2,548 yd away; an instance or
// battleground map 30 / 42 / 90) or that is hostile (RpgTravelDestination::IsActive drops it only after the choice), and held
// for nothing. A spawn is reachable on an open-world map with a playerbot travel node within kMaxNodeYards (2D, CLI-PATH's rule).
constexpr float kMaxNodeYards = 500.0f;

inline bool SpawnReachable(bool openWorldMap, float nearestNodeYards)
{
    return openWorldMap && nearestNodeYards <= kMaxNodeYards;
}

enum class TripTarget
{
    Kept,
    Hostile,
    Unreachable
};

// A trip target entry: hostile first (no trip to an enemy at all), then reachable = one of its spawns is.
inline TripTarget ClassifyTripTarget(bool hostile, bool anySpawnReachable)
{
    if (hostile)
        return TripTarget::Hostile;
    if (!anySpawnReachable)
        return TripTarget::Unreachable;
    return TripTarget::Kept;
}
}
