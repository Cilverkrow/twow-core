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
// A started trip holds its travel condition this long (the trip itself, the gap may end on the way).
constexpr std::uint32_t kTripHoldSeconds = 1800;
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

// startedAt = the last request of this kind (0 = never).
inline bool TripHolds(std::uint32_t now, std::uint32_t startedAt)
{
    return startedAt != 0 && now < startedAt + kTripHoldSeconds;
}

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
}
