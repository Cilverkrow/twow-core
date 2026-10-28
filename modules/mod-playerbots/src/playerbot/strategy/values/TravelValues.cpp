#include "playerbot/playerbot.h"
#include "TravelValues.h"
#include "QuestValues.h"
#include "SharedValueContext.h"
#include "BudgetValues.h"
#include "GuildValues.h"
#include "playerbot/PersistentRosterProfessionTrainingPolicy.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Guild/GuildMgr.h"
#include "playerbot/TravelChoosePolicy.h"
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/QuestFailureClassPolicy.h"
#include "playerbot/QuestGrindModePolicy.h"
#include "playerbot/GatherPurposePolicy.h"
#include "playerbot/DirectTargetDestinations.h"
#include "playerbot/ProfessionTravelPolicy.h"   // twow-repo#333 Berufe P1-a / P1-b
#include "playerbot/CohortPolicy.h"
#include "playerbot/TravelMgr.h"    // P1-b2: the destinations' spawn points
#include "playerbot/TravelNode.h"   // P1-b2: the travel graph
#include "TrainerValues.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <tuple>

using namespace ai;

namespace
{
ai::travel_choose::ParkingLot<std::future<PartitionedTravelList>> parkingLot;
std::atomic<uint32> parkedSince{ 0 };
std::atomic<uint32> capHitsSince{ 0 };
std::atomic<uint32> lastSummary{ 0 };
std::atomic<uint32> lastCollectMs{ 0 };
}

void FutureDestinations::Park()
{
    if (!future.valid() || future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        return;

    parkingLot.Park(std::move(future));
    future = std::future<PartitionedTravelList>();
    ++parkedSince;
}

size_t FutureDestinations::ParkedCount()
{
    return parkingLot.Count();
}

bool FutureDestinations::MayStart()
{
    if (!parkingLot.Full())
        return true;

    ++capHitsSince;
    return false;
}

void FutureDestinations::Collect()
{
    uint32 const nowMs = WorldTimer::getMSTime();
    uint32 lastMs = lastCollectMs.load();
    if (WorldTimer::getMSTimeDiff(lastMs, nowMs) < ai::travel_choose::CollectIntervalMs ||
        !lastCollectMs.compare_exchange_strong(lastMs, nowMs))
        return;

    size_t const pending = parkingLot.Count() ? parkingLot.Pending() : 0;

    uint32 const now = uint32(time(nullptr));
    uint32 last = lastSummary.load();
    if (now - last < ai::travel_choose::LogSeconds || !(pending || parkedSince.load() || capHitsSince.load()))
        return;
    if (!lastSummary.compare_exchange_strong(last, now))
        return;

    sLog.outBasic("[TravelChoose] state=summary pending=%u parked=%u parked_max=%u cap_hits=%u cap=%u",
        uint32(pending), parkedSince.exchange(0), uint32(parkingLot.Max()), capHitsSince.exchange(0),
        uint32(ai::travel_choose::ParkCap));
}

namespace
{
bool UsesQuestFirstProgression(Player const* bot)
{
    return bot && sPlayerbotAIConfig.questFirstProgressionEnabled &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());
}

bool UsesFreePersistentRosterProfessionTraining(Player const* bot)
{
    if (!bot || !sPlayerbotAIConfig.professionTrainingFreeForPersistentRoster)
        return false;

    uint32 const pair = sRandomPlayerbotMgr.GetProfessionPair(bot->GetGUIDLow());
    return profession_training::IsEligible(
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()),
        bot->GetLevel(), sPlayerbotAIConfig.professionTrainingStartLevel,
        static_cast<profession::Pair>(pair));
}

// twow-repo#333 Berufe P1-a / P1-b: an open turn-in (the 8.5 rule of the gather purposes; with turn-in parking only an
// open, not parked one).
bool ProfessionTripTurnInWaiting(Player* bot, TravelTarget const* travelTarget)
{
    TravelTarget const* turnIns = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures ? travelTarget : nullptr;
    uint32 finished = 0;
    for (auto const& [questId, status] : bot->getQuestStatusMap())
        if (!status.m_rewarded && status.m_status == QUEST_STATUS_COMPLETE && (!turnIns || turnIns->IsTurnInOpen(questId)))
            ++finished;
    return ai::quest_search::GatherYieldsToTurnIn(true, finished);
}

// twow-repo#333 Berufe P1-a / P1-b (ProfessionTravelPolicy.h): may this bot start a profession trip of this kind now?
// The cheap gate first (mode, roster bot on its own: no real master, no group; level, hourly limit), then the quest-first
// gap (CLI-Deep 25.10.): the 8.1 quest route backoff runs, no open turn-in waits (the 8.5 rule of the gather purposes), no
// band walk runs.
// The gap values are only read, never written.
bool ProfessionTripStartable(PlayerbotAI* ai, uint32 mode, char const* tripValue)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    uint32 const now = uint32(time(nullptr));

    bool const applies = ai::cohort::Applies(mode, bot->GetGUIDLow());
    // OB-10 26.10. (P1-n review): not in a group - a trip of one member would pull it away from its group.
    bool const rosterOnItsOwn = applies && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        !ai->HasRealPlayerMaster() && !bot->GetGroup();
    uint32 const startedAt = applies ? uint32(std::max(0, AI_VALUE2(int, "manual int", tripValue))) : 0;
    if (!ai::profession_travel::MayStartTrip(applies, rosterOnItsOwn, bot->GetLevel(),
            ai::profession_travel::CooldownOver(now, startedAt)))
        return false;

    uint32 const backoffUntil = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until")));
    if (now >= backoffUntil)
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    return ai::profession_travel::InQuestGap(now, backoffUntil, ProfessionTripTurnInWaiting(bot, travelTarget),
        travelTarget->GetBandTripEnd());
}

// A started trip keeps its travel condition while its reason lasts, until the backoff of its start ended plus 10 min
// (at most 30 min, ProfessionTravelPolicy.h HoldUntil), and never past an open turn-in - the trip ends and the turn-in
// request runs. The first evaluation that ends an open trip writes one "[ProfTravel] state=end" line with the reason and
// hold_over_s (the seconds after the backoff end: the quest-time cost, CLI-Deep 25.10.). A check that still holds stores
// its time for a later "reason=lost" end (P1-b3).
bool ProfessionTripHolds(PlayerbotAI* ai, char const* tripValue, char const* kind, bool reasonLasts)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    std::string const key(tripValue);
    uint32 const now = uint32(time(nullptr));
    uint32 const holdUntil = uint32(std::max(0, AI_VALUE2(int, "manual int", key + ai::profession_travel::kHoldUntilSuffix)));
    char const* const end = ai::profession_travel::EndReason(reasonLasts,
        reasonLasts && ProfessionTripTurnInWaiting(bot, AI_VALUE(TravelTarget*, "travel target")),
        ai::profession_travel::TripHolds(now, holdUntil));
    if (!end)
    {
        SET_AI_VALUE2(int, "manual int", key + ai::profession_travel::kHeldAtSuffix, int(now));
        return true;
    }

    if (AI_VALUE2(int, "manual int", key + ai::profession_travel::kOpenSuffix))
    {
        SET_AI_VALUE2(int, "manual int", key + ai::profession_travel::kOpenSuffix, 0);
        uint32 const startedAt = uint32(std::max(0, AI_VALUE2(int, "manual int", key)));
        sLog.outBasic("[ProfTravel] state=end kind=%s bot=%u level=%u cohort=%c reason=%s trip_s=%u hold_over_s=%u path=%s",
            kind, bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), end,
            now > startedAt ? now - startedAt : 0, ai::profession_travel::HoldOverSeconds(now,
                uint32(std::max(0, AI_VALUE2(int, "manual int", key + ai::profession_travel::kBackoffEndSuffix)))),
            AI_VALUE2(int, "manual int", key + ai::profession_travel::kNearSuffix) ? "near" : "gap");
    }
    return false;
}

// P1-n (ProfessionTravelPolicy.h, AiPlayerbot.ProfessionTraining.NearRadius): may this bot start a near trainer trip now? No
// quest gap needed; the group, the open turn-in, the band walk, the money and the hourly limit as for the gap trip, at
// every calculation.
// Nothing while a travel target is prepared or active: the request could not act then (TravelActionMultiplier,
// RequestTravelTargetAction::isUseful), and with Perf.TravelRequestGate 0 the value is read every tick. The destination walk
// runs at most once per kNearCheckSeconds; between two walks its last result counts (pre-review: a false between the checks
// starved the trigger - a hub visit with a turn-in first lost its one chance). With ProfessionTraining.Trace one
// "[ProfTravel] state=gate" line per eligible bot and trace cooldown, from a walk or a refusing cheap gate (result=start: all
// passed).
bool ProfessionNearTripStartable(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    uint32 const radius = sPlayerbotAIConfig.professionTrainingNearRadius;
    if (!radius)
        return false;
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    if (travelTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE || AI_VALUE(bool, "travel target active"))
        return false;

    uint32 const now = uint32(time(nullptr));
    bool const applies = ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTravel, bot->GetGUIDLow());
    bool const rosterOnItsOwn = applies && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&
        !bot->GetGroup();
    bool const eligible = ai::profession_travel::MayStartTrip(applies, rosterOnItsOwn, bot->GetLevel(), true);
    bool const cooldownOver = ai::profession_travel::CooldownOver(now,
        uint32(std::max(0, AI_VALUE2(int, "manual int", ai::profession_travel::kTrainerTripValue))));

    // Each gate only when the ones before passed (NearGateReason names the first that refused).
    bool turnIn = false, band = false, money = true;
    if (eligible && cooldownOver)
    {
        turnIn = ProfessionTripTurnInWaiting(bot, travelTarget);
        band = travelTarget->GetBandTripEnd() != 0;
        if (!turnIn && !band)
            money = UsesFreePersistentRosterProfessionTraining(bot) ||
                AI_VALUE2(bool, "has all money for", (uint32)NeedMoneyFor::skilltraining);
    }
    bool const cheapGates = eligible && cooldownOver && !turnIn && !band && money;

    ai::NearTrainerCounts counts;
    ai::ProfessionTripDrops drops;
    bool walked = false, found = false;
    if (cheapGates)
    {
        if (ai::profession_travel::NearCheckDue(now, uint32(std::max(0, AI_VALUE2(int, "manual int", ai::profession_travel::kNearCheckValue)))))
        {
            SET_AI_VALUE2(int, "manual int", ai::profession_travel::kNearCheckValue, int(now));
            found = !ai::NearProfessionTrainers(ai, radius, &drops, &counts).empty();
            SET_AI_VALUE2(int, "manual int", ai::profession_travel::kNearResultValue, found ? 1 : 0);
            walked = true;
        }
        else
            found = AI_VALUE2(int, "manual int", ai::profession_travel::kNearResultValue) != 0;
    }
    // P1-u: a walk whose reachable trainers all lay outside the bot's zone refuses as home_zone, not as unreachable.
    char const* const refused = ai::profession_travel::NearGateReason(eligible, cooldownOver, turnIn, band, money,
        !walked || counts.trainers > 0, !walked || counts.inRadius > 0, !walked || found || drops.homeZone > 0, found);

    uint32 const tracedAt = uint32(std::max(0, AI_VALUE2(int, "manual int", ai::profession_travel::kNearTraceValue)));
    if (sPlayerbotAIConfig.professionTrainingTrace && eligible && (walked || !cheapGates) &&
        (tracedAt == 0 || now >= tracedAt + sPlayerbotAIConfig.professionTrainingTraceCooldownSeconds))
    {
        SET_AI_VALUE2(int, "manual int", ai::profession_travel::kNearTraceValue, int(now));
        sLog.outBasic("[ProfTravel] state=gate kind=trainer path=near bot=%u level=%u cohort=%c result=%s money=%u need=%u trainers=%u in_radius=%u dropped_hostile=%u dropped_unreachable=%u radius=%u dropped_home_zone=%u zone=%u",
            bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), refused ? refused : "start", bot->GetMoney(),
            AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::skilltraining), counts.trainers, counts.inRadius,
            drops.hostile, drops.unreachable, radius, drops.homeZone, bot->GetZoneId());
    }
    return !refused;
}

// P1-y (ProfessionTravelPolicy.h NearToolGateReason, OB-00 28.10.2026): may this bot start a near tool trip now? The near trainer
// trip's rules - no quest gap; group, open turn-in, band walk, money and the hourly limit (of the tool trip) at every calculation;
// nothing while a travel target is prepared or active; one destination walk per kNearCheckSeconds, its result counts between two
// walks - for the vendors of the missing pick / knife. A bot that misses no tool returns first, without a gate line. With
// ProfessionTraining.Trace one "[ProfTravel] state=gate kind=tool path=near" line per eligible bot and trace cooldown.
bool ProfessionNearToolTripStartable(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    uint32 const radius = sPlayerbotAIConfig.professionTrainingNearRadius;
    if (!radius)
        return false;
    uint32 const tool = ai::MissingGatherTool(ai);
    if (!tool)
        return false;
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    if (travelTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE || AI_VALUE(bool, "travel target active"))
        return false;

    uint32 const now = uint32(time(nullptr));
    bool const applies = ai::cohort::Applies(sPlayerbotAIConfig.professionUseToolBuy, bot->GetGUIDLow());
    bool const rosterOnItsOwn = applies && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&
        !bot->GetGroup();
    bool const eligible = ai::profession_travel::MayStartTrip(applies, rosterOnItsOwn, bot->GetLevel(), true);
    bool const cooldownOver = ai::profession_travel::CooldownOver(now,
        uint32(std::max(0, AI_VALUE2(int, "manual int", ai::profession_travel::kToolTripValue))));

    // Each gate only when the ones before passed (NearToolGateReason names the first that refused). Money as the gap tool trip.
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(tool);
    bool turnIn = false, band = false, money = true;
    if (eligible && cooldownOver)
    {
        turnIn = ProfessionTripTurnInWaiting(bot, travelTarget);
        band = travelTarget->GetBandTripEnd() != 0;
        if (!turnIn && !band)
            money = proto && AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill) >= proto->BuyPrice;
    }
    bool const cheapGates = eligible && cooldownOver && !turnIn && !band && money;

    ai::NearTrainerCounts counts;
    ai::ProfessionTripDrops drops;
    bool walked = false, found = false;
    if (cheapGates)
    {
        if (ai::profession_travel::NearCheckDue(now, uint32(std::max(0, AI_VALUE2(int, "manual int", ai::profession_travel::kNearToolCheckValue)))))
        {
            SET_AI_VALUE2(int, "manual int", ai::profession_travel::kNearToolCheckValue, int(now));
            found = !ai::NearGatherToolVendors(ai, tool, radius, &drops, &counts).empty();
            SET_AI_VALUE2(int, "manual int", ai::profession_travel::kNearToolResultValue, found ? 1 : 0);
            walked = true;
        }
        else
            found = AI_VALUE2(int, "manual int", ai::profession_travel::kNearToolResultValue) != 0;
    }
    char const* const refused = ai::profession_travel::NearToolGateReason(eligible, cooldownOver, turnIn, band, money,
        !walked || counts.trainers > 0, !walked || counts.inRadius > 0, !walked || found || drops.homeZone > 0, found);

    uint32 const tracedAt = uint32(std::max(0, AI_VALUE2(int, "manual int", ai::profession_travel::kNearToolTraceValue)));
    if (sPlayerbotAIConfig.professionTrainingTrace && eligible && (walked || !cheapGates) &&
        (tracedAt == 0 || now >= tracedAt + sPlayerbotAIConfig.professionTrainingTraceCooldownSeconds))
    {
        SET_AI_VALUE2(int, "manual int", ai::profession_travel::kNearToolTraceValue, int(now));
        sLog.outBasic("[ProfTravel] state=gate kind=tool path=near bot=%u level=%u cohort=%c result=%s tool=%u money=%u need=%u vendors=%u in_radius=%u dropped_hostile=%u dropped_unreachable=%u radius=%u dropped_home_zone=%u zone=%u",
            bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), refused ? refused : "start", tool, bot->GetMoney(),
            proto ? proto->BuyPrice : 0, counts.trainers, counts.inRadius, drops.hostile, drops.unreachable, radius, drops.homeZone,
            bot->GetZoneId());
    }
    return !refused;
}
}

std::vector<int32> ai::PhaseOneProfessionTrainers(PlayerbotAI* ai, bool onlyUnlearned)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    std::vector<int32> trainers;

    // The roster filter of "trainable spells" keeps only the planned pair and the secondary skills; GREEN is the core's.
    std::vector<TrainerSpell const*> const trainable = AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells",
        int32(TRAINER_TYPE_TRADESKILLS));
    if (trainable.empty())
        return trainers;

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");
    auto const tradeskills = spellMap->find(TRAINER_TYPE_TRADESKILLS);
    if (tradeskills == spellMap->end())
        return trainers;

    // For tradeskill trainers the key is the skill line (TrainableSpellMapValue).
    for (auto const& [skill, spellTrainers] : tradeskills->second)
    {
        if (!ai::profession_travel::IsPhaseOneSkill(skill))
            continue;
        // P1-n: the near trip only for a skill not learned yet.
        if (onlyUnlearned && !ai::profession_travel::NearTripSkill(skill, ai->HasSkill(SkillType(skill))))
            continue;

        for (auto const& [trainerSpell, entries] : spellTrainers)
        {
            // Ranks only (the spell teaches the skill, as GetTrainerSpellState's profession check): the bucket of a skill
            // also holds its recipes (Smelt Tin at mining 65, Heavy Linen Bandage at first aid 40), not worth a trip.
            // P1-t (ProfessionTravelPolicy.h TeachesProfessionRank): learnedSpell is the teaching spell here, the rank is
            // the spell it teaches.
#ifdef MANGOSBOT_ZERO
            uint32 const teaching = trainerSpell->learnedSpell;
#else
            uint32 const teaching = trainerSpell->learnedSpell[0];
#endif
            SpellEntry const* teachingSpell = sServerFacade.LookupSpellInfo(teaching);
            if (!teachingSpell || !ai::profession_travel::TeachesProfessionRank(teaching,
                    { teachingSpell->Effect[0], teachingSpell->Effect[1], teachingSpell->Effect[2] },
                    { teachingSpell->EffectTriggerSpell[0], teachingSpell->EffectTriggerSpell[1], teachingSpell->EffectTriggerSpell[2] },
                    [](uint32 spell) { return sSpellMgr.IsProfessionSpell(spell); }))
                continue;

            if (std::find(trainable.begin(), trainable.end(), trainerSpell) == trainable.end())
                continue;

            for (int32 const entry : entries)
                if (std::find(trainers.begin(), trainers.end(), entry) == trainers.end())
                    trainers.push_back(entry);
        }
    }

    return trainers;
}

uint32 ai::MissingGatherTool(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return ai::profession_travel::MissingTool(
        ai->HasSkill(SKILL_MINING), bot->HasItemCount(ai::profession_travel::kMiningPick, 1),
        ai->HasSkill(SKILL_SKINNING), bot->HasItemCount(ai::profession_travel::kSkinningKnife, 1));
}

std::vector<int32> ai::GatherToolVendors(PlayerbotAI* ai, uint32 tool)
{
    std::vector<int32> vendors;
    if (tool)
        for (int32 const entry : GAI_VALUE2(std::list<int32>, "item vendor list", tool))
            vendors.push_back(entry);
    return vendors;
}

namespace
{
// P1-b2: the reachability of an entry, once per server run (its spawns and the travel graph stay). Keyed by entry, purpose,
// side (GetDestinations drops the other side's start-zone destinations, CustomRaceStartZones) and map:
// - the node rule's verdict, when the entry had destinations: for every asker (map key kAnyAskerMap);
// - "no destination in this call" (a template without spawns in "item vendor list" / the trainable spell map, or one
//   GetDestinations dropped by the asker's distance): only for the asker's map (ProfessionTravelPolicy.h ReachCacheScope),
//   so the start condition (every 5 s in the gap) does not walk the destinations again and no other map inherits the drop.
using ProfessionTripReachKey = std::tuple<int32, uint32, bool, uint32>;
constexpr uint32 kAnyAskerMap = 0xFFFFFFFF;
std::mutex professionTripReachMutex;
std::map<ProfessionTripReachKey, bool> professionTripReach;

// The 2D distance from a spawn to the nearest travel node on its map (CLI-PATH's nearest_node_yd), FLT_MAX without one.
float NearestTravelNodeYards(WorldPosition const& point, std::vector<TravelNode*> const& nodes)
{
    float nearestSq = FLT_MAX;
    for (TravelNode* node : nodes)
    {
        WorldPosition const* nodePos = node->getPosition();
        if (nodePos->getMapId() == point.getMapId())
            nearestSq = std::min(nearestSq, point.sqDistance2d(*nodePos));
    }
    return nearestSq == FLT_MAX ? FLT_MAX : std::sqrt(nearestSq);
}

// Is one spawn of this destination reachable (ProfessionTravelPolicy.h SpawnReachable)?
bool ProfessionTripDestinationReachable(TravelDestination const* destination, std::vector<TravelNode*> const& nodes)
{
    for (WorldPosition const* point : destination->GetPoints())
        if (point && ai::profession_travel::SpawnReachable(point->isOverworld(), NearestTravelNodeYards(*point, nodes)))
            return true;
    return false;
}

// P1-u: the zones of an entry's spawns, once per server run and side (its spawns stay). From the area flag the destination
// points carry since the start ("entry guidps", FetchArea): no terrain lookup here. An entry without a destination for
// this asker is not stored (asked again next time).
using ProfessionTripZoneKey = std::tuple<int32, uint32, bool>;
std::map<ProfessionTripZoneKey, std::vector<uint32>> professionTripZones;

std::vector<uint32> ProfessionTripSpawnZones(Player* bot, int32 entry, TravelDestinationPurpose purpose)
{
    ProfessionTripZoneKey const key(entry, uint32(purpose), bot->GetTeam() == ALLIANCE);
    {
        std::lock_guard<std::mutex> lock(professionTripReachMutex);
        auto const found = professionTripZones.find(key);
        if (found != professionTripZones.end())
            return found->second;
    }

    std::vector<uint32> zones;
    bool hadDestination = false;
    for (TravelDestination* destination : sTravelMgr.GetDestinations(PlayerTravelInfo(bot), uint32(purpose), { entry }, false, 0.0f))
    {
        hadDestination = true;
        for (WorldPosition const* point : destination->GetPoints())
            if (point)
                zones.push_back(TerrainManager::GetZoneIdByAreaFlag(point->getAreaFlag(), point->getMapId()));
    }
    if (hadDestination)
    {
        std::lock_guard<std::mutex> lock(professionTripReachMutex);
        professionTripZones[key] = zones;
    }
    return zones;
}
}

std::vector<int32> ai::ProfessionTripTargets(PlayerbotAI* ai, std::vector<int32> const& entries, TravelDestinationPurpose purpose,
    ProfessionTripDrops* drops)
{
    Player* bot = ai->GetBot();
    bool const alliance = bot->GetTeam() == ALLIANCE;
    uint32 const askerMap = bot->GetMapId();
    std::vector<int32> kept;
    std::vector<int32> unknown;
    std::unordered_map<int32, bool> reach;
    {
        std::lock_guard<std::mutex> lock(professionTripReachMutex);
        for (int32 const entry : entries)
        {
            auto found = professionTripReach.find(ProfessionTripReachKey(entry, uint32(purpose), alliance, kAnyAskerMap));
            if (found == professionTripReach.end())
                found = professionTripReach.find(ProfessionTripReachKey(entry, uint32(purpose), alliance, askerMap));
            if (found != professionTripReach.end())
                reach[entry] = found->second;
            else if (std::find(unknown.begin(), unknown.end(), entry) == unknown.end())   // a vendor list may hold an entry twice
                unknown.push_back(entry);
        }
    }

    // The entries not asked yet: their destinations of this purpose (the same list GetPartitions builds from). An entry
    // without one never comes back from GetPartitions either: unreachable for this asker. While the travel graph is busy
    // or empty (startup) the unknown entries are kept and nothing is stored - asked again next time.
    if (!unknown.empty())
    {
        auto const started = std::chrono::steady_clock::now();
        // The destinations first, without the graph lock (their distance code may read the graph itself).
        DestinationList const destinations = sTravelMgr.GetDestinations(PlayerTravelInfo(bot), uint32(purpose), unknown, false, 0.0f);

        // Shared, never blocking for long; held only while the node positions are read (the graph writers take it exclusively).
        std::shared_lock<std::shared_timed_mutex> graphLock(sTravelNodeMap.m_nMapMtx, std::defer_lock);
        std::vector<TravelNode*> nodes;
        if (graphLock.try_lock_for(std::chrono::milliseconds(50)))
            nodes = sTravelNodeMap.getNodes();

        if (nodes.empty())
            for (int32 const entry : unknown)
                reach[entry] = true;
        else
        {
            std::set<int32> withDestination;
            for (TravelDestination* destination : destinations)
            {
                withDestination.insert(destination->GetEntry());
                bool& entryReach = reach[destination->GetEntry()];
                entryReach = entryReach || ProfessionTripDestinationReachable(destination, nodes);
            }
            graphLock.unlock();

            uint32 reachable = 0;
            {
                std::lock_guard<std::mutex> lock(professionTripReachMutex);
                for (int32 const entry : unknown)
                {
                    bool const hadDestination = withDestination.count(entry) != 0;
                    reachable += reach[entry] ? 1 : 0;
                    uint32 const mapKey = ai::profession_travel::ReachCacheScope(hadDestination) ==
                        ai::profession_travel::ReachCache::EveryAsker ? kAnyAskerMap : askerMap;
                    professionTripReach[ProfessionTripReachKey(entry, uint32(purpose), alliance, mapKey)] = reach[entry];
                }
            }

            // Once per newly asked set (the first request of a kind pays it, later ones hit the cache): the cost on the bot
            // thread - destinations x spawn points x travel nodes.
            sLog.outBasic("[ProfTravel] state=reach purpose=%u bot=%u map=%u asked=%u destinations=%u no_destination=%u reachable=%u nodes=%u us=%u",
                uint32(purpose), bot->GetGUIDLow(), askerMap, uint32(unknown.size()), uint32(destinations.size()),
                uint32(unknown.size() - withDestination.size()), reachable, uint32(nodes.size()),
                uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()));
        }
    }

    // P1-u (ProfessionTravelPolicy.h HomeZoneOnly): below level 10 only an entry with a spawn in the bot's zone, for every
    // profession trip (trainer near and gap, tool vendor).
    bool const homeZoneOnly = ai::profession_travel::HomeZoneOnly(bot->GetLevel());
    uint32 const botZone = homeZoneOnly ? bot->GetZoneId() : 0;
    for (int32 const entry : entries)
    {
        auto const found = reach.find(entry);
        bool const hostile = GuidPosition(HIGHGUID_UNIT, uint32(entry)).IsHostileTo(bot);   // as RpgTravelDestination::IsActive
        switch (ai::profession_travel::ClassifyTripTarget(hostile, found != reach.end() && found->second))
        {
        case ai::profession_travel::TripTarget::Kept:
            if (homeZoneOnly && !ai::profession_travel::SpawnInHomeZone(botZone, ProfessionTripSpawnZones(bot, entry, purpose)))
            {
                if (drops)
                    ++drops->homeZone;
                break;
            }
            kept.push_back(entry);
            break;
        case ai::profession_travel::TripTarget::Hostile:
            if (drops)
                ++drops->hostile;
            break;
        case ai::profession_travel::TripTarget::Unreachable:
            if (drops)
                ++drops->unreachable;
            break;
        }
    }
    return kept;
}

namespace
{
// The entries with a destination within the radius. GetDestinations drops a destination farther than the radius;
// WithinNearRadius states the rule on the same distance. Only on the bot's map: DistanceTo to another map is a portal /
// transport estimate (pre-review), no "near".
std::vector<int32> NearTripEntries(PlayerbotAI* ai, std::vector<int32> const& entries, TravelDestinationPurpose purpose, uint32 radius)
{
    PlayerTravelInfo const info(ai->GetBot());
    WorldPosition const center = info.GetPosition();
    std::vector<int32> inRadius;
    for (TravelDestination* destination : sTravelMgr.GetDestinations(info, uint32(purpose), entries, false, float(radius)))
        if (destination->HasPointsOnMap(center) && ai::profession_travel::WithinNearRadius(destination->DistanceTo(center), radius) &&
            std::find(inRadius.begin(), inRadius.end(), destination->GetEntry()) == inRadius.end())
            inRadius.push_back(destination->GetEntry());
    return inRadius;
}
}

std::vector<int32> ai::NearProfessionTrainers(PlayerbotAI* ai, uint32 radius, ProfessionTripDrops* drops, NearTrainerCounts* counts)
{
    std::vector<int32> const trainers = PhaseOneProfessionTrainers(ai, true);
    if (counts)
        counts->trainers = uint32(trainers.size());
    if (trainers.empty() || !radius)
        return {};

    std::vector<int32> const inRadius = NearTripEntries(ai, trainers, TravelDestinationPurpose::Trainer, radius);
    if (counts)
        counts->inRadius = uint32(inRadius.size());
    return inRadius.empty() ? inRadius : ProfessionTripTargets(ai, inRadius, TravelDestinationPurpose::Trainer, drops);
}

std::vector<int32> ai::NearGatherToolVendors(PlayerbotAI* ai, uint32 tool, uint32 radius, ProfessionTripDrops* drops, NearTrainerCounts* counts)
{
    std::vector<int32> const vendors = GatherToolVendors(ai, tool);
    if (counts)
        counts->trainers = uint32(vendors.size());
    if (vendors.empty() || !radius)
        return {};

    std::vector<int32> const inRadius = NearTripEntries(ai, vendors, TravelDestinationPurpose::Vendor, radius);
    if (counts)
        counts->inRadius = uint32(inRadius.size());
    return inRadius.empty() ? inRadius : ProfessionTripTargets(ai, inRadius, TravelDestinationPurpose::Vendor, drops);
}

// P1-v (OB-00 26.10.2026, M1r run 3): GetPartitions passes every point through IsLocationLevelValid, whose fight gate (bot
// level - 2 - deaths against the area level, an unknown area refused) applies to trainers and vendors too - a level 5-9 bot
// never got the Goldshire trainer, the list came back empty and SetBestTarget failed without a line (20 requests, 0
// targets). The entries are vetted already (ProfessionTripTargets: friendly, reachable, P1-u home zone); the same closest
// partition per destination as GetPartitions, its nearest point, no fight gate.
PartitionedTravelList ai::ProfessionTripPartitions(WorldPosition const& center, std::vector<uint32> const& partitions,
    PlayerTravelInfo const& info, TravelDestinationPurpose purpose, std::vector<int32> const& entries, uint32 homeZone)
{
    // GetPartitions' default distance cap (pre-review: without it a level 10+ bot could get a trainer on the other continent).
    float const maxDistance = 10000.0f;
    PartitionedTravelList list;
    sTravelMgr.GetPartitionsLock();
    for (TravelDestination* destination : sTravelMgr.GetDestinations(info, uint32(purpose), entries, false, maxDistance))
    {
        std::pair<uint32, std::vector<WorldPosition*>> const range = destination->GetClosestPartition(center, partitions);
        if (!range.first)
            continue;

        WorldPosition* nearest = nullptr;
        float nearestDistance = 0.0f;
        for (WorldPosition* position : range.second)
        {
            if (!position || (homeZone && !ai::profession_travel::SpawnInHomeZone(homeZone,
                    { TerrainManager::GetZoneIdByAreaFlag(position->getAreaFlag(), position->getMapId()) })))
                continue;
            float const distance = position->distance(center);
            if (distance > maxDistance)
                continue;
            if (!nearest || distance < nearestDistance)
            {
                nearest = position;
                nearestDistance = distance;
            }
        }
        // GetPartitions keeps only a point with a distance above 0 (a bot on the spot still gets its trip).
        if (nearest)
            list[range.first].push_back(TravelPoint(destination, nearest, std::max(nearestDistance, 0.1f)));
    }
    sTravelMgr.GetPartitionsLock(false);
    return list;
}

void ai::ProfessionTripNoTarget(PlayerbotAI* ai, std::string const& travelPurpose, uint32 ranges, uint32 points,
    std::string const& rejects)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    bool const trainerTrip = travelPurpose != "tool vendor" && travelPurpose != "tool vendor near";
    std::string const key = trainerTrip ? ai::profession_travel::kTrainerTripValue : ai::profession_travel::kToolTripValue;
    char const* const kind = trainerTrip ? "trainer" : "tool";
    char const* const path = travelPurpose == "trainer profession near" || travelPurpose == "tool vendor near" ? "near" : "gap";
    sLog.outBasic("[ProfTravel] state=no_target kind=%s bot=%u level=%u cohort=%c ranges=%u points=%u path=%s rejects=%s",
        kind, bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), ranges, points, path, rejects.c_str());

    if (!AI_VALUE2(int, "manual int", key + ai::profession_travel::kOpenSuffix))
        return;
    SET_AI_VALUE2(int, "manual int", key + ai::profession_travel::kOpenSuffix, 0);
    sLog.outBasic("[ProfTravel] state=end kind=%s bot=%u level=%u cohort=%c reason=no_target trip_s=0 hold_over_s=0 path=%s",
        kind, bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), path);
}

EntryGuidps EntryGuidpsValue::Calculate()
{
    EntryGuidps guidps;

    for (auto& creatureDataPair : WorldPosition().getCreaturesNear())
    {
        AsyncGuidPosition aGuidP(creatureDataPair);
        if (aGuidP.isValid() && !aGuidP.IsEventUnspawned())
        {
            aGuidP.FetchArea();
            guidps[aGuidP.GetEntry()].push_back(aGuidP);
        }
    }

    for (auto& goDataPair : WorldPosition().getGameObjectsNear())
    {
        AsyncGuidPosition aGuidP(goDataPair);
        if (aGuidP.isValid() && !aGuidP.IsEventUnspawned())
        {
            aGuidP.FetchArea();
            guidps[-((int32)aGuidP.GetEntry())].push_back(aGuidP);
        }
    }

    return guidps;
}

EntryTravelPurposeMap EntryTravelPurposeMapValue::Calculate()
{
    EntryQuestRelationMap relationMap = GAI_VALUE(EntryQuestRelationMap, "entry quest relation");
    EntryGuidps guidpMap = GAI_VALUE(EntryGuidps, "entry guidps");

    EntryTravelPurposeMap entryPurposeMap;

    std::vector<NPCFlags> allowedNpcFlags;

    allowedNpcFlags.push_back(UNIT_NPC_FLAG_INNKEEPER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_GOSSIP);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_QUESTGIVER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_FLIGHTMASTER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_BANKER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_AUCTIONEER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_STABLEMASTER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_PETITIONER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_TABARDDESIGNER);

    allowedNpcFlags.push_back(UNIT_NPC_FLAG_TRAINER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_VENDOR);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_REPAIR);

    std::unordered_map<NPCFlags, TravelDestinationPurpose> npcPurposeMap =
    {
        { UNIT_NPC_FLAG_REPAIR, TravelDestinationPurpose::Repair },
        { UNIT_NPC_FLAG_VENDOR, TravelDestinationPurpose::Vendor },
        { UNIT_NPC_FLAG_TRAINER, TravelDestinationPurpose::Trainer },
        { UNIT_NPC_FLAG_AUCTIONEER, TravelDestinationPurpose::AH }
    };

    for (uint32 entry : sCreatureStorage.Ids())
    {
        CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(entry);

        if (!cInfo)
            continue;

        if (cInfo->ExtraFlags & CREATURE_EXTRA_FLAG_INVISIBLE)
            continue;

        DestinationPurose purpose = 0;

        if(relationMap.find(entry) != relationMap.end())
            for(auto& [questId, questFlag] : relationMap.at(entry))
                purpose |= questFlag;

        for (auto& flag : allowedNpcFlags)
        {
            if ((cInfo->NpcFlags & flag) != 0)
            {
                purpose |= (uint32)TravelDestinationPurpose::GenericRpg;
                break;
            }
        }

        for (auto& [flag, flagPurpose] : npcPurposeMap)
        {
            if ((cInfo->NpcFlags & flag) != 0)
            {
                purpose |= (uint32)flagPurpose;
            }
        }


        if (cInfo->MinLootGold > 0)
        {
            purpose |= (uint32)TravelDestinationPurpose::Grind;
        }
        else  //Added these specifically because dk start mobs drop no money but have sellable loot.
        {
            switch (entry)
            {
                case 28611: //Scarlet Captain           1
                case 28530: //Scarlet Commander         2
                case 28936: //Scarlet Commander         4
                case 29000: //Scarlet Commander Rodrick 4
                case 28529: //Scarlet Crusader          2
                case 28940: //Scarlet Crusader          4
                case 28609: //Scarlet Infantryman       1
                case 28610: //Scarlet Marksman          4
                case 28608: //Scarlet Medic             1
                case 28819: //Scarlet Miner             1
                case 28822: //Scarlet Miner             1
                case 28557: //Scarlet Peasant           1
                case 28594: //Scarlet Preacher          2
                case 28939: //Scarlet Preacher          4
                    purpose |= (uint32)TravelDestinationPurpose::Grind;
                    break;
            }
        }

        if (cInfo->Rank == CREATURE_ELITE_ELITE || cInfo->Rank == CREATURE_ELITE_RAREELITE || cInfo->Rank == CREATURE_ELITE_WORLDBOSS || cInfo->Rank == CREATURE_ELITE_RARE)
        {
            if (cInfo->Rank == 1)
            {
                if (guidpMap[entry].size() == 1)
                    purpose |= (uint32)TravelDestinationPurpose::Boss;
            }
            else
                purpose |= (uint32)TravelDestinationPurpose::Boss;
        }

        if (cInfo->SkinningLootId && cInfo->GetRequiredLootSkill() == SKILL_SKINNING)
        {
            purpose |= (uint32)TravelDestinationPurpose::GatherSkinning;
        }

        if (uint32 skillId = SkillIdToGatherEntry(entry))
        {
            if (skillId == SKILL_SKINNING)
                purpose |= (uint32)TravelDestinationPurpose::GatherSkinning;
            if (skillId == SKILL_MINING)
                purpose |= (uint32)TravelDestinationPurpose::GatherMining;
            if (skillId == SKILL_HERBALISM)
                purpose |= (uint32)TravelDestinationPurpose::GatherHerbalism;
        }

        if (purpose > 0)
            entryPurposeMap[entry] = purpose;
    }

    for (uint32 entry : sGOStorage.Ids())
    {
        GameObjectInfo const* gInfo = sObjectMgr.GetGameObjectInfo(entry);

        if (!gInfo)
            continue;

        // Penqle's GameObjectInfo has no ExtraFlags (cmangos uses it for invisible
        // markers; the bot is checking a creature flag here — likely a bot-side
        // bug). Skip the invisibility check; harmless until a host hook lands.

        uint32 purpose = 0;

        DestinationEntry goEntry = entry * -1;

        if (relationMap.find(goEntry) != relationMap.end())
            for (auto& [questId, questFlag] : relationMap.at(goEntry))
                purpose |= questFlag;

        std::vector<GameobjectTypes> allowedGoTypes;

        allowedGoTypes.push_back(GAMEOBJECT_TYPE_MAILBOX);

        for (auto type : allowedGoTypes)
        {
            if (gInfo->type == type)
            {
                purpose |= (uint32)TravelDestinationPurpose::Mail;
                break;
            }
        }

        if (uint32 skillId = SkillIdToGatherEntry(goEntry))
        {
            if (skillId == SKILL_SKINNING)
                purpose |= (uint32)TravelDestinationPurpose::GatherSkinning;
            if (skillId == SKILL_MINING)
                purpose |= (uint32)TravelDestinationPurpose::GatherMining;
            if (skillId == SKILL_HERBALISM)
                purpose |= (uint32)TravelDestinationPurpose::GatherHerbalism;
        }

        if (purpose > 0)
            entryPurposeMap[goEntry] = purpose;
    }

    return entryPurposeMap;
}

uint32 EntryTravelPurposeMapValue::SkillIdToGatherEntry(int32 entry)
{
    if (entry > 0)
    {
        CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(entry);

        if (!cInfo->SkinningLootId)
            return 0;

        return cInfo->GetRequiredLootSkill();
    }
    else
    {
        GameObjectInfo const* gInfo = sObjectMgr.GetGameObjectInfo(entry * -1);

        if (uint32 lockId = gInfo->GetLockId())
        {
            LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
            if (lockInfo)
            {
                uint32 skillId = SKILL_NONE;

                for (int i = 0; i < 8; ++i)
                {
                    if (lockInfo->Type[i] != LOCK_KEY_SKILL)
                        continue;

                    if (SkillByLockType(LockType(lockInfo->Index[i])) == 0)
                        continue;

                    return SkillByLockType(LockType(lockInfo->Index[i]));
                }
            }
        }
    }

    return 0;
}

bool NeedTravelPurposeValue::Calculate()
{
    TravelDestinationPurpose purpose = TravelDestinationPurpose(stoi(getQualifier()));

    const std::map<TravelDestinationPurpose, SkillType> gatheringSkills =
    { {TravelDestinationPurpose::GatherFishing, SKILL_FISHING}
        , {TravelDestinationPurpose::GatherSkinning, SKILL_SKINNING}
        , {TravelDestinationPurpose::GatherMining, SKILL_MINING}
        , {TravelDestinationPurpose::GatherHerbalism, SKILL_HERBALISM}
    };

    SkillType skill;

    switch (purpose)
    {
    case TravelDestinationPurpose::Repair:
        if (AI_VALUE2(bool, "group or", "should repair,can repair,following party"))
            return true;
        if (AI_VALUE2(bool, "has strategy", "free") && AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair"))
            return true;
        break;
    case TravelDestinationPurpose::Vendor:
        if (AI_VALUE2(bool, "group or", "should sell,can sell,following party"))
            return true;
        if (AI_VALUE2(bool, "has strategy", "free") && AI_VALUE(bool, "should sell") && AI_VALUE(bool, "can sell"))
            return true;
        break;
    case TravelDestinationPurpose::AH:
        if (AI_VALUE2(bool, "group or", "should ah sell,can ah sell,following party"))
            return true;
        if (AI_VALUE2(bool, "has strategy", "free") && AI_VALUE(bool, "should ah sell") && AI_VALUE(bool, "can ah sell"))
            return true;
        break;
    case TravelDestinationPurpose::GatherFishing:
        if (!AI_VALUE2(bool, "has strategy", "tfish"))
            return false;
    case TravelDestinationPurpose::GatherSkinning:
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherHerbalism:
    {
        // twow-repo#601 GrindMode 2 (review): a skinning trip is a kill trip - GatherTravelDestination::IsActive needs
        // living creatures of the entry, and the grind target filter refuses that kill, so the bot would travel, stand
        // and log "opportunistic" until the work time ran out. No skinning purpose for the population (no [Purpose]
        // start); skinning the corpses of its own kills, mining, herbalism and fishing stay. GrindMode 0 / 1: false.
        if (purpose == TravelDestinationPurpose::GatherSkinning && ai->IsQuestNoGrindTrip())   // + QuestFirstOrder (#601 review)
            return false;

        // Hotfix 8.5: finished quests are handed in before the bot gathers again.
        bool const rosterOnItsOwn = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
        // twow-repo#485: with turn-in parking on, only a quest the bot can hand in and has not
        // parked holds gathering back (the test of the taker fetch); 0 counts every completed one.
        TravelTarget const* turnIns = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures ? AI_VALUE(TravelTarget*, "travel target") : nullptr;
        uint32 finished = 0;
        if (rosterOnItsOwn)
            for (auto const& [questId, status] : bot->getQuestStatusMap())
                if (!status.m_rewarded && status.m_status == QUEST_STATUS_COMPLETE && (!turnIns || turnIns->IsTurnInOpen(questId)))
                    ++finished;
        if (ai::quest_search::GatherYieldsToTurnIn(rosterOnItsOwn, finished))
            return false;

        // twow-repo#333 Berufe P1-b (AiPlayerbot.ProfessionUse.ToolBuy): no mining or skinning trip without the tool -
        // the loot check refuses the node anyway (LootObjectStack.cpp), so the bot only stood there until the 15-minute
        // no-skill-up end and then blocked the skill for an hour. Before Decide, so no purpose is declared. Off: as before.
        if (ai::cohort::Applies(sPlayerbotAIConfig.professionUseToolBuy, bot->GetGUIDLow()) &&
            !ai::profession_travel::HasGatherTool(gatheringSkills.at(purpose),
                bot->HasItemCount(ai::profession_travel::kMiningPick, 1), bot->HasItemCount(ai::profession_travel::kSkinningKnife, 1)))
            return false;

        // Hotfix 8.5 (owner decision): a roster bot on its own gathers only under a
        // declared purpose with a skill target and a budget.
        skill = gatheringSkills.at(purpose);
        uint32 const value = bot->GetSkillValue(skill);
        uint32 const target = std::min<uint32>(bot->GetSkillMax(skill), bot->GetSkillMaxForLevel(bot));
        uint32 const now = uint32(time(nullptr));
        ai::gather_purpose::State& declared = ai->GetGatherPurpose();
        switch (ai::gather_purpose::Decide(declared, rosterOnItsOwn, skill, value, target, now))
        {
            case ai::gather_purpose::Decision::Start:
                declared.Start(skill, value, target, now);
                ai->GetFishingTrace().Reset();
                sLog.outBasic("[Purpose] state=start bot=%u level=%u profession=%s reason=skill_behind skill=%u target=%u budget_min=%u",
                    bot->GetGUIDLow(), bot->GetLevel(), ai::gather_purpose::ProfessionName(skill), value, target,
                    ai::gather_purpose::BudgetSeconds / 60);
                return true;
            case ai::gather_purpose::Decision::Yes:
                return true;
            default:
                return false;
        }
    }
    case TravelDestinationPurpose::Boss:
        // twow-repo#601 GrindMode 2 (review): a boss trip is a kill trip the grind target filter refuses unless the boss
        // attacks or a quest needs it (quest bosses come through the quest objective destinations). GrindMode 0 / 1: as before.
        if (ai->IsQuestNoGrindTrip())   // + QuestFirstOrder (#601 review)
            return false;
        return AI_VALUE(bool, "can fight boss");
    case TravelDestinationPurpose::Mail:
        return AI_VALUE(bool, "can get mail") && AI_VALUE(bool, "should get mail");
    case TravelDestinationPurpose::Explore:
        return ai->HasStrategy("explore", BotState::BOT_STATE_NON_COMBAT);    
    case TravelDestinationPurpose::GenericRpg:
    {
        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

        if (rpgPhase < 15) //Only last 45 minutes of the hour allow generic rpg.
            return false;

        return !AI_VALUE2(bool, "manual bool", "is travel refresh");
    }
    case TravelDestinationPurpose::Grind:
    {
        // Quest objectives can still point at creatures that also carry the
        // Grind bit. This branch is only the generic travel-purpose fallback;
        // objective destinations are built independently by the quest path.
        if (UsesQuestFirstProgression(bot))
        {
            // twow-repo#601 S1 (AiPlayerbot.QuestFirstProgression.FailureClasses, OB-10 answer 3): the 8.5 gather rule
            // generalised to grind - a quest-first bot on its own does not grind while a routable open turn-in waits:
            // one the last quest request validated, or one finished since (not checked yet - the next request checks
            // it). An open turn-in the check turned down does not hold grinding back, and none does while the quest route
            // backoff runs (RequestQuestTravelTargetAction waits for it - the bot would stand idle). Off: as before.
            if (sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) &&
                !ai->HasRealPlayerMaster())
            {
                bool const routeBackoffRunning =
                    uint32(time(nullptr)) < uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until")));
                TravelTarget const* turnIns = AI_VALUE(TravelTarget*, "travel target");
                std::string const checked = AI_VALUE2(std::string, "manual string", ai::quest_failure_class::CheckedTurnInsValue);
                std::string const validated = AI_VALUE2(std::string, "manual string", ai::quest_failure_class::ValidatedTurnInsValue);
                uint32 routableOpen = 0;
                for (auto const& [questId, status] : bot->getQuestStatusMap())
                    if (!status.m_rewarded && status.m_status == QUEST_STATUS_COMPLETE && turnIns->IsTurnInOpen(questId) &&
                        (ai::quest_failure_class::ListHas(validated, questId) || !ai::quest_failure_class::ListHas(checked, questId)))
                        ++routableOpen;
                if (ai::quest_failure_class::GrindYieldsToTurnIn(true, routableOpen, routeBackoffRunning))
                    return false;
            }

            // twow-repo#601 S2 / S4 R1 (GrindMode 1, OB-10 rev. 2): the gap filler grinds only while the quest route backoff
            // runs (the last quest search found nothing, any S1 class - a transient one too, OP4) and S1's turn-in yield
            // above did not hold; never as a refresh renewal. It returns before the S0 GrindFallbackMinutes, the
            // FreshGrindFailures and the 8.32 GrindFallbackFailures lines below, so S0 stays GrindMode 0 (never both).
            // GrindMode 0 / 2: IsQuestGapGrind() is false, everything below as before.
            if (ai->IsQuestGapGrind())
                return ai::quest_grind_mode::GapWindowOpen(
                    uint32(time(nullptr)) < uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until"))),
                    AI_VALUE2(bool, "manual bool", "is travel refresh"));

            // Hotfix 8.32 (twow-repo#544): after repeated empty quest route searches a quest-first
            // bot grinds instead of idling ("no destination").
            uint32 const failures = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route failures")));
            // twow-repo#601 GrindMode 2 (owner 17.10.2026): no grind fallback as a substitute activity - the bot stands
            // idle; [QuestIdle] reason=fallback when the 8.32 rule (fresh threshold included) would have let it grind.
            // GrindMode 0 / 1: IsQuestNoGrind() is false, everything below as before.
            if (ai->IsQuestNoGrind())
            {
                uint32 const minFailures = ai::quest_search::FreshBot(sPlayerbotAIConfig.directTargetsEnabled, bot->GetLevel(),
                    sPlayerbotAIConfig.directTargetsFreshMaxLevel) && sPlayerbotAIConfig.directTargetsFreshGrindFailures ?
                    sPlayerbotAIConfig.directTargetsFreshGrindFailures : sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures;
                if (ai::quest_search::GrindFallback(true, failures, minFailures) && !AI_VALUE2(bool, "manual bool", "is travel refresh"))
                    ai->NoteQuestIdle(uint8(ai::quest_grind_mode::Reason::Fallback),
                        ai::quest_grind_mode::TargetText("grind_fallback_failures", failures));
                return false;
            }
            // twow-repo#601 (AiPlayerbot.QuestFirstProgression.GrindFallbackMinutes): the fallback holds only that long after
            // the last empty quest route search; then the quest request (6.3, below grind 6.35) can fire again. 0 = for good.
            if (!ai::quest_search::GrindFallbackFresh(sPlayerbotAIConfig.questFirstProgressionGrindFallbackMinutes,
                    uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route failed at"))), uint32(time(nullptr))))
                return false;
            // twow-repo#594 / P3-a (AiPlayerbot.DirectTargets.FreshGrindFailures): a fresh bot may grind after fewer.
            if (ai::quest_search::FreshBot(sPlayerbotAIConfig.directTargetsEnabled, bot->GetLevel(), sPlayerbotAIConfig.directTargetsFreshMaxLevel) &&
                sPlayerbotAIConfig.directTargetsFreshGrindFailures)
                return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.directTargetsFreshGrindFailures) &&
                    !AI_VALUE2(bool, "manual bool", "is travel refresh");
            return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures) &&
                !AI_VALUE2(bool, "manual bool", "is travel refresh");
        }

        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

        if (rpgPhase > 45) //Only first 45 minutes of the hour allow generic grind.
            return false;

        return !AI_VALUE2(bool, "manual bool", "is travel refresh");
    }
    // twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.Station / Cooking / Dungeon / Raid, default 0): built at boot and
    // switched on, a roster bot on its own; the need per class is in DirectTargetDestinations.cpp.
    case TravelDestinationPurpose::Station:
    case TravelDestinationPurpose::DungeonEntrance:
    case TravelDestinationPurpose::RaidEntrance:
        return ai::direct_targets::NeedPurpose(ai, purpose);
    default:
        return false;
    }

    return false;
}

bool ShouldTravelNamedValue::Calculate()
{
    std::string name = getQualifier();

    WorldPosition botPos(bot);

    if (name == "city")
    {
        if (bot->GetLevel() <= 5)
            return false;

        if (!botPos.isOverworld())
            return false;

        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

        if (rpgPhase > 20) //Only first 20 minutes of the hour allow generic city pvp without reason.
            return false;

        if (AI_VALUE2(bool, "manual bool", "is travel refresh"))
            return false;

        return true;
    }
    else if (name == "pvp")
    {
        if (bot->GetLevel() <= 50)
            return false;

        int32 rpgStyle = AI_VALUE2(int32, "manual saved int", "rpg style override");

        if (rpgStyle < 0)
            rpgStyle = ai->GetFixedBotNumber(BotTypeNumber::RPG_STYLE_NUMBER, 100);

        if (rpgStyle > 10) //Only 10% of the bots like to go to world-pvp.
            return false;

        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

        if (rpgPhase > 15) //Only first 15 minutes of the hour allow world pvp.
            return false;

        if (!botPos.isOverworld())
            return false;

        return true;
    }
    else if (name == "guild meeting")
    {
        if (!bot->GetGuildId())
            return false;

        Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
        if (!guild)
            return false;

        std::string motd = guild->GetMOTD();
        if (motd.empty()) 
            return false;

        // Parse guild MOTD for the meeting time.
        // Meeting: <location> <start time> <end time>
        auto pos = motd.find("Meeting:");
        if (pos == std::string::npos)
            return false;

        std::string body = motd.substr(pos + 8);
        std::vector<std::string> tokens;
        { std::istringstream iss(body); std::string t; while (iss >> t) tokens.push_back(t); }
        if (tokens.size() < 3)
            return false;

        auto parseTime = [](const std::string& tok, int& h, int& m) -> bool {
            auto colon = tok.find(':');
            if (colon == std::string::npos) return false;
            h = std::stoi(tok.substr(0, colon));
            std::string rest = tok.substr(colon + 1);
            std::string digits, suffix;
            for (char c : rest) { if (std::isdigit(c)) digits += c; else suffix += (char)toupper(c); }
            m = std::stoi(digits);
            if (h < 0 || h > 23 || m < 0 || m > 59) return false;
            if (suffix == "PM" && h != 12) h += 12;
            if (suffix == "AM" && h == 12) h = 0;
            return true;
        };

        int sh, sm, eh, em;
        if (!parseTime(tokens[tokens.size() - 2], sh, sm)) return false;
        if (!parseTime(tokens[tokens.size() - 1], eh, em)) return false;

        time_t now = time(nullptr);
        tm local = *localtime(&now);
        tm startTm = local; startTm.tm_hour = sh; startTm.tm_min = sm; startTm.tm_sec = 0;
        tm endTm = local;   endTm.tm_hour = eh;   endTm.tm_min = em;   endTm.tm_sec = 0;
        time_t start = mktime(&startTm);
        time_t end = mktime(&endTm);
        if (end < start) end += 24 * 3600;

        return (now >= start - 30 * 60) && (now <= end);
    }
    else if (name == "guild order")
    {
        return AI_VALUE(bool, "has guild travel order");
    }
    else if (name == "reagent vendor")
    {
        return AI_VALUE(bool, "needs profession reagents");
    }
    // twow-repo#333 Berufe P1-a (AiPlayerbot.ProfessionTraining.Travel): a gap trip to a trainer of a phase-one profession
    // with a GREEN rank for the bot now. Before the generic "trainer" branch, whose "trainer trade" stays refused for roster
    // bots (#306). The "hold" name is the condition of a started trip.
    else if (name == "trainer profession" || name == "trainer profession hold")
    {
        uint32 const mode = sPlayerbotAIConfig.professionTrainingTravel;
        if (name == "trainer profession hold")
            return ai::cohort::Applies(mode, bot->GetGUIDLow()) &&
                ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, "trainer", !PhaseOneProfessionTrainers(ai).empty());

        if (!ProfessionTripStartable(ai, mode, ai::profession_travel::kTrainerTripValue))
            return false;

        if (!UsesFreePersistentRosterProfessionTraining(bot) &&
            !AI_VALUE2(bool, "has all money for", (uint32)NeedMoneyFor::skilltraining))
            return false;

        // P1-b2: only a trainer the bot can reach and that is not hostile starts a trip (no request, no hourly limit spent).
        return !ProfessionTripTargets(ai, PhaseOneProfessionTrainers(ai), TravelDestinationPurpose::Trainer).empty();
    }
    // P1-n (AiPlayerbot.ProfessionTraining.NearRadius, OB-00 26.10.): a trip to a trainer of a phase-one skill not learned yet
    // within the radius, without a quest gap. The same trip value as the gap trip; its hold lasts while an unlearned skill is
    // still to learn (learned on the way, e.g. at a local trainer: done).
    else if (name == "trainer profession near" || name == "trainer profession near hold")
    {
        if (name == "trainer profession near hold")
            return ai::cohort::Applies(sPlayerbotAIConfig.professionTrainingTravel, bot->GetGUIDLow()) &&
                ProfessionTripHolds(ai, ai::profession_travel::kTrainerTripValue, "trainer", !PhaseOneProfessionTrainers(ai, true).empty());

        return ProfessionNearTripStartable(ai);
    }
    // twow-repo#333 Berufe P1-b (AiPlayerbot.ProfessionUse.ToolBuy): a gap trip to a vendor of the missing pick or knife.
    else if (name == "tool vendor" || name == "tool vendor hold")
    {
        uint32 const mode = sPlayerbotAIConfig.professionUseToolBuy;
        if (name == "tool vendor hold")
            return ai::cohort::Applies(mode, bot->GetGUIDLow()) &&
                ProfessionTripHolds(ai, ai::profession_travel::kToolTripValue, "tool", MissingGatherTool(ai) != 0);

        if (!ProfessionTripStartable(ai, mode, ai::profession_travel::kToolTripValue))
            return false;

        uint32 const tool = MissingGatherTool(ai);
        if (!tool)
            return false;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(tool);
        if (!proto || AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill) < proto->BuyPrice)
            return false;

        // P1-b2: only a vendor the bot can reach and that is not hostile starts a trip.
        return !ProfessionTripTargets(ai, GatherToolVendors(ai, tool), TravelDestinationPurpose::Vendor).empty();
    }
    // P1-y (OB-00 28.10.2026): a near trip to a vendor of the missing tool within ProfessionTraining.NearRadius, without a quest
    // gap. The same trip value as the gap tool trip; its hold lasts while the tool is still missing (bought on the way: done).
    else if (name == "tool vendor near" || name == "tool vendor near hold")
    {
        if (name == "tool vendor near hold")
            return ai::cohort::Applies(sPlayerbotAIConfig.professionUseToolBuy, bot->GetGUIDLow()) &&
                ProfessionTripHolds(ai, ai::profession_travel::kToolTripValue, "tool", MissingGatherTool(ai) != 0);

        return ProfessionNearToolTripStartable(ai);
    }
    else if (name == "mount")
    {
        if (AI_VALUE(bool, "can buy mount"))
            return true;

        return false;
    }
    else if (name.find("trainer") == 0)
    {
        if (ai->HasRealPlayerMaster())
            return false;

        // A persistent roster profession pair authorizes only local,
        // opportunistic training. Do not turn a missing planned profession
        // into a city, map, transport, hearth, or teleport trip. The normal
        // RPG trainer action remains available after ordinary gameplay has
        // brought the bot into local interaction range.
        if (name == "trainer trade" &&
            !profession_training::MayRequestRemoteTrainerTravel(
                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()), true))
            return false;

        TrainerType trainerType = TRAINER_TYPE_CLASS;
        NeedMoneyFor budgetType = NeedMoneyFor::spells;

        if (name == "trainer mount")
        {
            trainerType = TRAINER_TYPE_MOUNTS;
            budgetType = NeedMoneyFor::mount; //Only train mounts when you can actually buy mount
        }
        if (name == "trainer trade")
        {
            trainerType = TRAINER_TYPE_TRADESKILLS;
            budgetType = NeedMoneyFor::skilltraining;
        }
        if (name == "trainer pet")
        {
            trainerType = TRAINER_TYPE_PETS;
            budgetType = NeedMoneyFor::anything;
        }

        if (AI_VALUE2(uint32, "train cost", trainerType) == 0) //Has nothing to train
            return false;

        bool const freePersistentRosterProfessionTraining =
            trainerType == TRAINER_TYPE_TRADESKILLS &&
            UsesFreePersistentRosterProfessionTraining(bot);
        if (!freePersistentRosterProfessionTraining &&
            !AI_VALUE2(bool, "has all money for", (uint32)budgetType))
            return false;

        return true;
    }

    return false;
}

bool TravelTargetActiveValue::Calculate() 
{
    return AI_VALUE(TravelTarget*, "travel target")->IsActive();
};

bool TravelTargetReadyValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_READY;
};

bool TravelTargetTravelingValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL;
};

bool TravelTargetWorkingValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK;
};

bool QuestStageActiveValue::Calculate()
{
    uint32 questId = getMultiQualifierInt(getQualifier(), 0, ",");
    TravelDestinationPurpose purpose = (TravelDestinationPurpose)getMultiQualifierInt(getQualifier(), 1, ",");

    uint32 objective = 0;

    switch (purpose)
    {
    case TravelDestinationPurpose::QuestGiver:
        if (bot->HasQuest(questId))
            return false;
        break;
    case TravelDestinationPurpose::QuestTaker:
        // CanCompleteQuest answers "could this quest still flip to complete", and
        // the core returns false the moment it has - "not allow re-complete quest".
        // Asking it here threw away exactly the quests that were ready to hand in,
        // leaving only the brief window where the objectives are met but the status
        // has not been updated yet. Bots that missed that window carried the quest
        // forever: ten characters followed from level 1 accumulated 25 completed
        // quests between them without a single QuestTravelToTaker event.
        if (bot->GetQuestRewardStatus(questId))
            return false;
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !bot->CanCompleteQuest(questId))
            return false;
        break;
    case TravelDestinationPurpose::QuestObjective1:
        objective = 1;
        break;
    case TravelDestinationPurpose::QuestObjective2:
        objective = 2;
        break;
    case TravelDestinationPurpose::QuestObjective3:
        objective = 3;
        break;
    case TravelDestinationPurpose::QuestObjective4:
        objective = 4;
        break;
    }

    if(objective)
        if (!AI_VALUE2(bool, "need quest objective", "{" + std::to_string(questId) + "," + std::to_string(objective - 1) + "}"))
            return false;

    return true;
}
