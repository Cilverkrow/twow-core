#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// P1-b3 (CLI-Deep R-f 25.10.2026): the end time of a trip whose target went away without a failed hold check (another
// target took over). Its "reason=lost" line is only written at the next request of the kind, often an hour later, so it
// counts to the last hold check that still held (heldAt), or to the start when none ran.
inline std::uint32_t LostEndAt(std::uint32_t startedAt, std::uint32_t heldAt)
{
    return heldAt > startedAt ? heldAt : startedAt;
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
constexpr char const* kHeldAtSuffix = " held at";

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

// P1-n (OB-00 26.10.2026 after M1 FUTILE): the gap trip found almost no gap - 26 quest route rejections in 57 min for 30 bots,
// most with a finished quest, 0 trainer requests in M1 and in cr2-C. "trainer profession near"
// (AiPlayerbot.ProfessionTraining.NearRadius, yards, 0 = off) sends a bot that has not learned a planned phase-one skill yet to
// a matching trainer within the radius - typically on a city or hub visit - without a quest gap. Relevance 6.36: above grind
// (6.35) and the quest request (6.3), the detour is bounded by the radius. Not past an open turn-in, not during a band walk,
// within the hourly limit of the trainer trip (the same "manual int").
constexpr std::uint32_t kNearHoldSeconds = 600;
// The near check walks the trainer destinations: at most once per bot and kNearCheckSeconds.
constexpr std::uint32_t kNearCheckSeconds = 60;
constexpr std::uint32_t kMaxNearRadius = 1000;

// "Once" (OB-00): the near trip teaches only a phase-one skill the bot does not have yet; the next ranks stay with the local
// trainer path and the gap trip.
inline bool NearTripSkill(std::uint32_t skill, bool hasSkill)
{
    return IsPhaseOneSkill(skill) && !hasSkill;
}

inline bool WithinNearRadius(float distance, std::uint32_t nearRadius)
{
    return nearRadius > 0 && distance >= 0.0f && distance <= float(nearRadius);
}

// A near trip holds kNearHoldSeconds from its request (no backoff to follow); its whole time counts as hold_over_s.
inline std::uint32_t NearHoldUntil(std::uint32_t now)
{
    return now + kNearHoldSeconds;
}

// "manual int" values: the time of the last near check and of the last gate trace line (0 = never).
constexpr char const* kNearCheckValue = "profession near check at";
constexpr char const* kNearTraceValue = "profession near trace at";
// The result of the last destination walk (1 = a trainer within the radius), counted until the next walk.
constexpr char const* kNearResultValue = "profession near found";
// Appended to kTrainerTripValue: 1 = the open trip is a near trip (path=near on its request and end lines).
constexpr char const* kNearSuffix = " near";

// lastCheck = the time of the last near check (0 = never).
inline bool NearCheckDue(std::uint32_t now, std::uint32_t lastCheck)
{
    return lastCheck == 0 || now >= lastCheck + kNearCheckSeconds;
}

// The config value: 0 or less = off, above kMaxNearRadius = kMaxNearRadius (a "near" trip is no continent walk).
inline std::uint32_t ClampNearRadius(std::int32_t configured)
{
    if (configured <= 0)
        return 0;
    return std::uint32_t(configured) > kMaxNearRadius ? kMaxNearRadius : std::uint32_t(configured);
}

// The first gate of a near check that refused, for the "[ProfTravel] state=gate" trace (nullptr = all passed). eligible = the
// switch applies, a roster bot on its own, level >= kMinLevel. homeZoneTrainers (P1-u): a reachable trainer is left after the
// home zone rule (always true from kHomeZoneBelowLevel on).
inline char const* NearGateReason(bool eligible, bool cooldownOver, bool turnInWaiting, bool bandWalk, bool money, bool trainers,
    bool nearTrainers, bool reachableTrainers, bool homeZoneTrainers)
{
    if (!eligible)
        return "not_eligible";
    if (!cooldownOver)
        return "cooldown";
    if (turnInWaiting)
        return "turn_in";
    if (bandWalk)
        return "band";
    if (!money)
        return "money";
    if (!trainers)
        return "no_trainer";
    if (!nearTrainers)
        return "not_near";
    if (!reachableTrainers)
        return "unreachable";
    if (!homeZoneTrainers)
        return "home_zone";
    return nullptr;
}

// P1-y (OB-00 28.10.2026 after M1r run 5 FAIL on the tool criterion: the tool trip started only in a quest gap - two skinners
// went 3 h 45 min without a knife and without one tool request, two miners bought the pick 4 min after learning where a vendor
// stood near). "tool vendor near" (AiPlayerbot.ProfessionUse.ToolBuy, the radius of ProfessionTraining.NearRadius) sends a bot
// with mining or skinning but without its tool to a vendor of it with a spawn within the radius, without a quest gap, under the
// gates of the near trainer trip and within the hourly limit of the tool trip (kToolTripValue, shared with the gap trip). A bot
// that misses no tool never asks and writes no gate line.
inline char const* NearToolGateReason(bool eligible, bool cooldownOver, bool turnInWaiting, bool bandWalk, bool money, bool vendors,
    bool nearVendors, bool reachableVendors, bool homeZoneVendors)
{
    if (!eligible)
        return "not_eligible";
    if (!cooldownOver)
        return "cooldown";
    if (turnInWaiting)
        return "turn_in";
    if (bandWalk)
        return "band";
    if (!money)
        return "money";
    if (!vendors)
        return "no_vendor";
    if (!nearVendors)
        return "not_near";
    if (!reachableVendors)
        return "unreachable";
    if (!homeZoneVendors)
        return "home_zone";
    return nullptr;
}

// "manual int" values of the near tool check, apart from the trainer's (one bot may run both checks in one tick).
constexpr char const* kNearToolCheckValue = "profession near tool check at";
constexpr char const* kNearToolTraceValue = "profession near tool trace at";
constexpr char const* kNearToolResultValue = "profession near tool found";

// P1-u (owner 26.10.2026 via OB-00: early access to the professions yes, leaving the start area only for a profession no;
// OB-00 go X = 10): below kHomeZoneBelowLevel a profession trip (trainer near or gap,
// tool vendor) only goes to an entry with a spawn in the zone the bot stands in - the zone, not the sub-area (Elwynn Forest
// with Northshire and Goldshire, not Stormwind City). Without one there is no trip; the bot learns from level 10 on, or on
// its way as before (#306 local trainer path). A spawn of unknown zone (0) never counts.
constexpr std::uint32_t kHomeZoneBelowLevel = 10;

inline bool HomeZoneOnly(std::uint32_t level)
{
    return level < kHomeZoneBelowLevel;
}

inline bool SpawnInHomeZone(std::uint32_t botZone, std::vector<std::uint32_t> const& spawnZones)
{
    return botZone != 0 && std::find(spawnZones.begin(), spawnZones.end(), botZone) != spawnZones.end();
}

// P1-t (M1r FUTILE 26.10.2026, gate trace trainers=0 for all 30 bots): the rank filter of the trainer list asked
// IsProfessionSpell of TrainerSpell::learnedSpell. In this core learnedSpell is a union alias of spell - the trainer's
// teaching spell (2581 Apprentice Miner: LEARN_SPELL 2575, effect 2 SKILL_STEP), not the taught one (2575 Mining: effect 2
// SKILL) - so every rank was dropped and no trip could start. A trainer spell teaches a rank when it is a profession spell
// itself (cmangos: learnedSpell is the taught spell) or one of its LEARN_SPELL effects teaches one (the core's own
// GetTrainerSpellState walks EffectTriggerSpell the same way). A recipe (Smelt Tin) teaches a spell without a SKILL effect.
constexpr std::uint32_t kSpellEffectLearnSpell = 36;
constexpr std::size_t kSpellEffects = 3;

template <typename IsProfessionSpell>
inline bool TeachesProfessionRank(std::uint32_t spell, std::array<std::uint32_t, kSpellEffects> const& effects,
    std::array<std::uint32_t, kSpellEffects> const& triggers, IsProfessionSpell isProfessionSpell)
{
    if (isProfessionSpell(spell))
        return true;
    for (std::size_t i = 0; i < kSpellEffects; ++i)
        if (effects[i] == kSpellEffectLearnSpell && triggers[i] && isProfessionSpell(triggers[i]))
            return true;
    return false;
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
// Deliberately open world only: the two trainers CLI-PATH rates reachable through in-instance travel nodes (Annora 11073 in
// Uldaman, map 70; Henry Stern 8696 in Razorfen Downs, map 129) are dropped - a dungeon walk is no quest-gap filler.
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

// Whom one lookup's verdict holds for (CLI-PATH review 25.10.): the node rule's verdict on an entry's destinations for
// every asker; "no destination in this call" only for the asker's map - GetDestinations drops a destination by the asker's
// distance (DistanceTo == FLT_MAX), so another map must ask again instead of inheriting a permanent drop.
enum class ReachCache
{
    EveryAsker,
    AskerMap
};

inline ReachCache ReachCacheScope(bool hadDestination)
{
    return hadDestination ? ReachCache::EveryAsker : ReachCache::AskerMap;
}
}
