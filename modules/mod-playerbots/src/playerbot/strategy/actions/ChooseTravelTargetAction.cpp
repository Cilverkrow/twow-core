
#include "playerbot/playerbot.h"
#include "playerbot/LootObjectStack.h"
#include "ChooseTravelTargetAction.h"
#include "playerbot/BotUpdateTrace.h"   // OB-10 18.10.: NowUs/SinceUs/Prevalidate for "TIME:prevalidate" and [PreValidate]
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/GatherNodePolicy.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "GuildCreateActions.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/RouteDangerPolicy.h"
#include "playerbot/StartZonePolicy.h"
#include "playerbot/DangerMapPolicy.h"
#include "playerbot/CampBlockPolicy.h"         // re-deaths at camps (DestinationDeaths.CampBlock)
#include "playerbot/CohortPolicy.h"            // re-deaths at camps (DangerMap.CountAddDeaths mode 2)
#include "playerbot/QuestTurnInParkPolicy.h"
#include "playerbot/QuestFailureClassPolicy.h"
#include "playerbot/QuestGrindModePolicy.h"
#include "playerbot/QuestFirstOrderPolicy.h"   // twow-repo#601 decision order (Grind.QuestFirstOrder)
#include "playerbot/QuestBandPolicy.h"         // twow-repo#601 S5a F1: the band empty streak value
#include "playerbot/QuestGroupWorld.h"         // twow-repo#627 S7 hook: the queue age for FailureClasses.GroupNeeded
#include "playerbot/StartupTravelPolicy.h"
#include "playerbot/ZoneExitPolicy.h"
#include "playerbot/DirectTargetDestinations.h"
#include "playerbot/ProfessionTravelPolicy.h"   // twow-repo#333 Berufe P1-a / P1-b
#include "playerbot/CohortPolicy.h"
#include "Guild/GuildMgr.h"
#include "Formulas.h"
#include <algorithm>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <vector>

using namespace ai;

namespace
{
danger_map::Params DangerMapParams()
{
    danger_map::Params params;
    params.cellSize = sPlayerbotAIConfig.dangerMapCellSize;
    params.windowSeconds = sPlayerbotAIConfig.dangerMapWindowSeconds;
    params.minDeaths = sPlayerbotAIConfig.dangerMapMinDeaths;
    params.levelMargin = sPlayerbotAIConfig.dangerMapLevelMargin;
    params.lineSamples = sPlayerbotAIConfig.dangerMapLineSamples;
    return params;
}

// Re-deaths at camps (AiPlayerbot.DangerMap.CountAddDeaths): the shared map is read with the add-death rule only for bots the
// switch applies to (mode 2 = cohort B); everything else as above.
danger_map::Params DangerMapParams(Player const* bot)
{
    danger_map::Params params = DangerMapParams();
    if (bot && ai::cohort::Applies(sPlayerbotAIConfig.dangerMapCountAddDeaths, bot->GetGUIDLow()))
        params.addDeathMinAttackers = sPlayerbotAIConfig.dangerMapAddDeathMinAttackers;
    return params;
}

bool UsesQuestFirstProgression(Player const* bot)
{
    return bot && sPlayerbotAIConfig.questFirstProgressionEnabled &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());
}

// twow-repo#601 GrindMode 2 (review): a destination whose work is killing creatures - a grind (money) mob, a boss, or a
// skinning destination (GatherTravelDestination::IsActive needs living creatures of the entry).
bool IsKillTripDestination(TravelDestination const* destination)
{
    return dynamic_cast<GrindTravelDestination const*>(destination) || dynamic_cast<BossTravelDestination const*>(destination) ||
        (destination->GetPurpose() == TravelDestinationPurpose::GatherSkinning && destination->GetEntry() > 0);
}

// #421 B: within each distance range, quest givers with more other givers
// around them (a quest hub) come first. Bounded: long ranges stay as they are.
void SortQuestHubsFirst(PartitionedTravelList& list)
{
    float const hubRadiusSq = ai::quest_search::HubRadius * ai::quest_search::HubRadius;
    for (auto& [partition, points] : list)
    {
        if (points.size() < 2 || points.size() > ai::quest_search::HubSortMaxPoints)
            continue;

        std::vector<uint32> score(points.size(), 0);
        for (size_t i = 0; i < points.size(); ++i)
        {
            TravelDestination* destination = std::get<0>(points[i]);
            WorldPosition* position = std::get<1>(points[i]);
            if (!destination || !position || destination->GetPurpose() != TravelDestinationPurpose::QuestGiver)
                continue;
            for (size_t j = 0; j < points.size(); ++j)
            {
                WorldPosition* other = std::get<1>(points[j]);
                TravelDestination* otherDestination = std::get<0>(points[j]);
                if (i != j && other && otherDestination && otherDestination->GetPurpose() == TravelDestinationPurpose::QuestGiver &&
                    position->sqDistance(*other) <= hubRadiusSq)
                    ++score[i];
            }
        }

        std::vector<size_t> order(points.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&score](size_t a, size_t b) { return score[a] > score[b]; });

        TravelPointList sorted;
        sorted.reserve(points.size());
        for (size_t i : order)
            sorted.push_back(points[i]);
        points.swap(sorted);
    }
}

bool IsGatherLeashPurpose(TravelDestinationPurpose purpose)
{
    return purpose == TravelDestinationPurpose::GatherSkinning ||
        purpose == TravelDestinationPurpose::GatherHerbalism ||
        purpose == TravelDestinationPurpose::GatherMining ||
        purpose == TravelDestinationPurpose::GatherFishing;
}

bool IsAutonomousRosterTravelBlocked(PlayerbotAI* ai, TravelDestinationPurpose purpose)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return UsesQuestFirstProgression(bot) && ai->HasRealPlayerMaster() && !IsGatherLeashPurpose(purpose);
}

bool HasQuestProgress(QuestStatusData const& status)
{
    if (status.m_explored)
        return true;

    for (uint8 objective = 0; objective < QUEST_OBJECTIVES_COUNT; ++objective)
        if (status.m_itemcount[objective] || status.m_creatureOrGOcount[objective])
            return true;

    return false;
}

bool IsProtectedFromQuestFirstRetirement(PlayerbotAI* ai, Quest const* quest, QuestStatusData const& status,
    focusQuestTravelList const& focusList)
{
    // Retiring is deliberately fail-closed. The only removable quest is an
    // entirely unprogressed, ordinary, autonomous quest with no item, chain,
    // access or player-directed semantics.
    if (status.m_status == QUEST_STATUS_COMPLETE || status.m_rewarded || HasQuestProgress(status))
        return true;
    if (quest->GetLimitTime() || quest->GetRequiredClasses() || quest->GetRequiredSkill() || quest->GetSrcItemId())
        return true;
    for (uint8 objective = 0; objective < QUEST_ITEM_OBJECTIVES_COUNT; ++objective)
        if (quest->ReqItemId[objective] || quest->ReqSourceId[objective])
            return true;
    if (quest->GetPrevQuestId() || quest->GetNextQuestId() || quest->GetNextQuestInChain() || quest->GetExclusiveGroup())
        return true;
    if (quest->GetType() == QUEST_TYPE_DUNGEON || quest->GetType() == QUEST_TYPE_RAID ||
        quest->HasQuestFlag(QUEST_FLAGS_RAID) || quest->GetSuggestedPlayers() || quest->IsRepeatable() ||
        focusList.find(quest->GetQuestId()) != focusList.end())
        return true;

    // No reliable per-quest master-pin exists in this path. Preserve every
    // quest while the bot has a real master rather than infer authorship.
    return ai->HasRealPlayerMaster();
}

bool RetireOneSafeStaleQuest(PlayerbotAI* ai, focusQuestTravelList const& focusList)
{
    Player* bot = ai->GetBot();
    if (!UsesQuestFirstProgression(bot))
        return false;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        QuestStatusMap::const_iterator statusIt = bot->getQuestStatusMap().find(questId);
        if (!quest || statusIt == bot->getQuestStatusMap().end())
            continue;

        if (bot->GetLevel() < quest->GetQuestLevel() + sPlayerbotAIConfig.questFirstProgressionRetireBelowLevelDelta ||
            IsProtectedFromQuestFirstRetirement(ai, quest, statusIt->second, focusList))
            continue;

        // Core owns every quest-item/source-item effect. This path only calls
        // the canonical quest-log removal after the conservative predicate.
        bot->RemoveQuestAtSlot(slot);
        if (!bot->GetQuestSlotQuestId(slot))
        {
            sLog.outDetail("[QuestFirst] retired_safe_stale_quest bot=%u quest=%u", bot->GetGUIDLow(), questId);
            return true;
        }
    }

    return false;
}

bool IsLocalActiveQuestHubDestination(Player* bot, TravelDestination* destination, WorldPosition const* position,
    PlayerTravelInfo const& travelInfo)
{
    QuestTravelDestination const* questDestination = dynamic_cast<QuestTravelDestination const*>(destination);
    return questDestination && questDestination->GetQuestId() && position &&
        position->getMapId() == bot->GetMapId() &&
        position->distance(WorldPosition(bot)) <= sPlayerbotAIConfig.questFirstProgressionLocalHubRadius &&
        destination->IsActive(bot, travelInfo);
}

// twow-repo#541 (audit A10): the cheap part of IsLocalActiveQuestHubDestination, without IsActive.
bool IsLocalQuestHubCandidate(Player* bot, TravelDestination* destination, WorldPosition const* position)
{
    QuestTravelDestination const* questDestination = dynamic_cast<QuestTravelDestination const*>(destination);
    return questDestination && questDestination->GetQuestId() && position &&
        position->getMapId() == bot->GetMapId() &&
        position->distance(WorldPosition(bot)) <= sPlayerbotAIConfig.questFirstProgressionLocalHubRadius;
}

uint32 GetZoneId(WorldPosition const& position)
{
    return sTerrainMgr.GetZoneId(position.getMapId(), position.getX(), position.getY(), position.getZ());
}

uint32 GetCompletedQuestId(Player* player)
{
    if (!player)
        return 0;

    for (auto const& [questId, status] : player->getQuestStatusMap())
        if (!status.m_rewarded && status.m_status == QUEST_STATUS_COMPLETE)
            return questId;

    return 0;
}

void TraceQuestRouteDecision(Player* bot, TravelTarget& target, std::string const& strategy, char const* reason)
{
    if (!UsesQuestFirstProgression(bot) || !sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions)
        return;

    TravelDestination const* destination = target.GetDestination();
    WorldPosition const* targetPosition = target.GetPosition();
    if (!destination || !targetPosition)
        return;

    QuestTravelDestination const* questDestination = dynamic_cast<QuestTravelDestination const*>(destination);
    WorldPosition const source(bot);
    std::string purpose = "unknown";
    if (auto const it = TravelDestinationPurposeName.find(destination->GetPurpose()); it != TravelDestinationPurposeName.end())
        purpose = it->second;

    // TravelMgr only exposes a route-qualified destination after GetPartitions
    // has rejected FLT_MAX/no-path points. Do not claim a specific transport
    // vehicle: cross-map is the actionable fact here, and the normal travel
    // planner remains the authority for ships, zeppelins and portals.
    char const* transport = source.getMapId() == targetPosition->getMapId() ? "same_map" : "cross_map_travelmgr";
    sLog.outBasic("[QuestFirstRoute] state=selected bot=%u level=%u quest=%u purpose=%s strategy=%s "
        "source_map=%u source_zone=%u target_entry=%d target_map=%u target_zone=%u transport=%s distance=%.0f reason=%s",
        bot->GetGUIDLow(), bot->GetLevel(), questDestination ? questDestination->GetQuestId() : 0,
        purpose.c_str(), strategy.c_str(), source.getMapId(), GetZoneId(source), destination->GetEntry(),
        targetPosition->getMapId(), GetZoneId(*targetPosition), transport, target.Distance(bot), reason);
}

void TraceQuestRouteRejection(Player* bot, std::string const& strategy, uint32 rangeCount, char const* reason,
    std::string const& detail = std::string())
{
    if (!UsesQuestFirstProgression(bot) || !sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions)
        return;

    WorldPosition const source(bot);
    sLog.outBasic("[QuestFirstRoute] state=rejected bot=%u level=%u quest=%u strategy=%s source_map=%u source_zone=%u "
        "offered_ranges=%u fallback=none reason=%s%s%s",
        bot->GetGUIDLow(), bot->GetLevel(), GetCompletedQuestId(bot), strategy.c_str(), source.getMapId(),
        GetZoneId(source), rangeCount, reason, detail.empty() ? "" : " ", detail.c_str());
}

// twow-repo#594 / P3-0 (AiPlayerbot.DirectTargets.TraceInactive): why IsActive turned this candidate down. Walks the
// checks of QuestRelationTravelDestination::IsActive in its order; any other destination type is "other". Only called
// for a candidate already turned down, only with the switch.
ai::quest_search::RejectCounts::InactiveReason InactiveReasonOf(PlayerbotAI* ai, TravelDestination* destination,
    PlayerTravelInfo const& info)
{
    using Counts = ai::quest_search::RejectCounts;
    QuestRelationTravelDestination* relation = dynamic_cast<QuestRelationTravelDestination*>(destination);
    if (!relation)
        return Counts::InactiveOther;
    if (!relation->IsPossible(info))
        return Counts::InactiveImpossible;

    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    std::string const entry = std::to_string(relation->GetEntry());
    if (relation->GetRelation() == 0)
    {
        if (!bot->GetMap()->IsContinent())
        {
            WorldPosition const* closestPoint = relation->GetClosestPoint(bot);
            if (!closestPoint || closestPoint->getMapId() != bot->GetMapId())
                return Counts::InactiveMap;
        }
        if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + entry))
        {
            if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + entry))
                return Counts::InactiveNoMark;
            if (!info.HasFocusQuest() && info.GetBoolValue("can fight equal") &&
                !AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + entry + ",need quest reward::" +
                    std::to_string(relation->GetQuestId())))
                return Counts::InactiveLowLevelNoReward;
        }
    }
    else if (!AI_VALUE2(bool, "group or", "following party,can turn in quest npc::" + entry))
        return Counts::InactiveNoTurnIn;

    if (relation->GetEntry() > 0 && GuidPosition(HIGHGUID_UNIT, relation->GetEntry()).IsHostileTo(bot))
        return Counts::InactiveHostile;
    return Counts::InactiveOther;
}

// twow-repo#485: the finished quests of this bot a turn-in-only quest route request offered
// takers for (comma separated, written by RequestQuestTravelTargetAction::Execute).
char const* const HandInQuestsValue = "future travel handin quests";

// Each of them counts as a failed turn-in when the request found no way to a taker
// (ai::turnin_park::CountsAsNoRoute). The list is read once and cleared. True when one of
// them was parked now.
bool NoteNoRouteTurnIns(AiObjectContext* context, TravelTarget* travelTarget, bool noRoute)
{
    if (!sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures)
        return false;

    std::string const quests = AI_VALUE2(std::string, "manual string", HandInQuestsValue);
    if (quests.empty())
        return false;

    SET_AI_VALUE2(std::string, "manual string", HandInQuestsValue, std::string());
    if (!noRoute)
        return false;

    bool parked = false;
    for (std::string const& questId : split(quests, ','))
        if (travelTarget->NoteTurnInFailure(uint32(atoi(questId.c_str())), "no_route"))
            parked = true;
    return parked;
}

// twow-repo#601 S1 (OB-10 answer 4): the per-candidate skips of SetBestTarget as one predicate. The main loop counts
// its answer per reason; with AiPlayerbot.QuestFirstProgression.FailureClasses the local quest hub pre-loop asks the
// same predicate, so a local candidate the main loop would skip no longer switches the hub filter on (soak 17.10.:
// 121 of 165 L10+ hub_filter rejections carried such a skip, none had hub_filter as the only reason).
enum class CandidateSkip
{
    None,
    TurnInSuppressed,
    DeathSuppressed,
    QuestLoopSuppressed,
    OtherMapGather,
    OtherMapGrind,
    GatherPurposeSuppressed,
};

CandidateSkip CandidateSkipOf(PlayerbotAI* ai, Player* bot, TravelTarget const* target, TravelTarget const* persistentTarget,
    TravelTarget const* excludedTurnInTarget, TravelDestination* destination, WorldPosition* position)
{
    if (excludedTurnInTarget && excludedTurnInTarget->IsTurnInRouteSuppressed(destination, position))
        return CandidateSkip::TurnInSuppressed;

    // #307: a destination this bot kept dying at is on a per-bot cooldown.
    if (persistentTarget && persistentTarget->IsDestinationDeathSuppressed(destination))
        return CandidateSkip::DeathSuppressed;

    // twow-repo#581 (QL-1): objectives of a quest this bot kept failing at (emptied camps) are skipped for a while.
    if (persistentTarget && persistentTarget->IsQuestLoopSuppressed(destination))
        return CandidateSkip::QuestLoopSuppressed;

    // #414: no gathering trips to another continent.
    uint32 const gatherPurposes = uint32(TravelDestinationPurpose::GatherSkinning) | uint32(TravelDestinationPurpose::GatherMining) |
        uint32(TravelDestinationPurpose::GatherHerbalism) | uint32(TravelDestinationPurpose::GatherFishing);
    if (position && !ai::gather_node::StaysOnMap((uint32(destination->GetPurpose()) & gatherPurposes) != 0,
            bot->GetMapId(), position->getMapId()))
        return CandidateSkip::OtherMapGather;

    // twow-repo#601 S2 R3 (GrindMode 1, OB-10 rev. 2): a gap grind trip stays on the bot's own map (precedent #414 above); a
    // forced target (guild order) is no gap grind trip. Only the gap grind choice (review C-F2: set only behind IsQuestGapGrind,
    // never for a quest choice of grind mobs); GrindMode 0 / 2: false before anything else.
    if (target->IsGapGrindChoice() && position && !target->IsForced() && !ai::quest_grind_mode::GapGrindStaysOnMap(true,
            (uint32(destination->GetPurpose()) & uint32(TravelDestinationPurpose::Grind)) != 0, bot->GetMapId(), position->getMapId()))
        return CandidateSkip::OtherMapGrind;

    // #422: a gathering purpose this bot died at twice without a killer.
    if (!target->IsForced() && ai->IsGatherPurposeSuppressed(uint32(destination->GetPurpose()) & gatherPurposes))
        return CandidateSkip::GatherPurposeSuppressed;

    return CandidateSkip::None;
}

// twow-repo#601 S3b (OB-10 rev. 2, Q-B2): the deferrals of SetBestTarget after IsActive as one predicate, in the main
// loop's order. Side-effect free: the main loop counts and traces its answer per reason (as before); with
// AiPlayerbot.QuestFirstProgression.HubPreloopSameFilters (and FailureClasses) the local quest hub pre-loop asks the same
// predicate, so a local candidate the main loop would defer no longer switches the hub filter on (soak 17.10.: 22 of 165
// L10+ hub_filter rejections). A danger map hit is handed back for the main loop's death_cluster line.
enum class PostActiveDeferral
{
    None,
    BattlegroundQuest,
    EnemyZone,
    CrossMap,
    ZoneLevel,
    DangerMap,
};

PostActiveDeferral PostActiveDeferralOf(PlayerbotAI* ai, Player* bot, TravelTarget const* target, TravelDestination* destination,
    WorldPosition* position, danger_map::DangerMap::Hit& dangerHit)
{
    // twow-repo#587 (Z3, AiPlayerbot.ZoneExit.SkipBattlegroundQuests): no battleground quest as a quest-first target.
    if (sPlayerbotAIConfig.zoneExitSkipBattlegroundQuests && !target->IsForced())
        if (QuestTravelDestination const* bgQuest = dynamic_cast<QuestTravelDestination const*>(destination))
            if (Quest const* bgQuestTemplate = sObjectMgr.GetQuestTemplate(bgQuest->GetQuestId()))
                if (ai::zone_exit::IsBattlegroundQuestSort(bgQuestTemplate->GetZoneOrSort()))
                    return PostActiveDeferral::BattlegroundQuest;

    // Checked after IsActive so the area lookup only happens for
    // the point that was actually selected.
    if (!target->IsForced() && position && position->isEnemyHomeZoneFor(bot->GetTeam()))
        return PostActiveDeferral::EnemyZone;

    // #307: a low-level roster bot defers a quest target whose route
    // is a continent switch or whose zone is clearly above its level,
    // instead of learning the danger by dying on the way (live: 81
    // deaths of level 2-4 bots on the Durotar -> Undercity turn-in).
    if (!target->IsForced() && position && UsesQuestFirstProgression(bot) &&
        dynamic_cast<QuestTravelDestination const*>(destination))
    {
        // Area (sub-zone) level: a zone row such as Redridge has 0, its
        // Lakeshire sub-area 15. getAreaLevel() resolves the real one.
        int32 const areaLevel = std::max<int32>(0, position->getAreaLevel());
        // twow-repo#485: CrossMapContinentsOnly limits the cross-map rule to
        // a route to the other continent; the tram counts as the Eastern
        // Kingdoms and an instance as the continent of its entrance.
        MapEntry const* const botMapEntry = bot->GetMap()->GetMapEntry();
        MapEntry const* const targetMapEntry = position->getMapEntry();
        route_danger::Reason const danger = route_danger::Classify(position->getMapId() != bot->GetMapId(),
            bot->GetLevel(), sPlayerbotAIConfig.questFirstProgressionMinLevelForCrossMapQuestRoute,
            uint32(areaLevel), ai::death_series::RouteMargin(ai->IsCautious()),
            sPlayerbotAIConfig.questFirstProgressionCrossMapContinentsOnly,
            route_danger::IsContinentSwitch(
                route_danger::ContinentOf(bot->GetMapId(), botMapEntry ? botMapEntry->ghostEntranceMap : -1),
                route_danger::ContinentOf(position->getMapId(), targetMapEntry ? targetMapEntry->ghostEntranceMap : -1)));
        if (danger == route_danger::Reason::CrossMap)
            return PostActiveDeferral::CrossMap;
        // twow-repo#575 (PATH-01 E5, AiPlayerbot.CustomRaceStartZones): up to level 12 a goblin / high elf
        // is never deferred by an area level inside its own start zone (sub-areas there are DBC level 5-10)
        // or, for the goblin, in Sparkwater Port (4021 = 36), where 41169 / 41170 change hands.
        if (danger == route_danger::Reason::TargetZoneLevel &&
            !ai::start_zone::ZoneLevelExempt(sPlayerbotAIConfig.customRaceStartZones, bot->getRace(), bot->GetLevel(),
                position->getMapId(), position->getX(), position->getY()))
            return PostActiveDeferral::ZoneLevel;
    }

    // #307: shared danger map. Any destination purpose; only roster
    // bots on their own, never a bot led by a real player.
    if (!target->IsForced() && position && sPlayerbotAIConfig.dangerMapEnabled &&
        UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster() && position->getMapId() == bot->GetMapId())
    {
        dangerHit = danger_map::Instance().Query(bot->GetMapId(),
            bot->GetPositionX(), bot->GetPositionY(), position->getX(), position->getY(),
            bot->GetLevel(), uint32(time(nullptr)), DangerMapParams(bot));
        if (dangerHit.cells)
            return PostActiveDeferral::DangerMap;
    }

    return PostActiveDeferral::None;
}

// twow-repo#601 S1: the route danger rule of SetBestTarget (#307, #485 CrossMapContinentsOnly, #575 start-zone
// exemption) for one taker point of the taker pre-validation - the same inputs as the main loop's check.
route_danger::Reason TakerRouteDanger(PlayerbotAI* ai, Player* bot, WorldPosition* point)
{
    int32 const areaLevel = std::max<int32>(0, point->getAreaLevel());
    MapEntry const* const botMapEntry = bot->GetMap()->GetMapEntry();
    MapEntry const* const pointMapEntry = point->getMapEntry();
    bool const continentsOnly = sPlayerbotAIConfig.questFirstProgressionCrossMapContinentsOnly;
    route_danger::Reason const danger = route_danger::Classify(point->getMapId() != bot->GetMapId(),
        bot->GetLevel(), sPlayerbotAIConfig.questFirstProgressionMinLevelForCrossMapQuestRoute,
        uint32(areaLevel), ai::death_series::RouteMargin(ai->IsCautious()),
        continentsOnly,
        route_danger::IsContinentSwitch(
            route_danger::ContinentOf(bot->GetMapId(), botMapEntry ? botMapEntry->ghostEntranceMap : -1),
            route_danger::ContinentOf(point->getMapId(), pointMapEntry ? pointMapEntry->ghostEntranceMap : -1)));
    if (danger == route_danger::Reason::TargetZoneLevel &&
        ai::start_zone::ZoneLevelExempt(sPlayerbotAIConfig.customRaceStartZones, bot->getRace(), bot->GetLevel(),
            point->getMapId(), point->getX(), point->getY()))
        return route_danger::Reason::None;
    return danger;
}

// twow-repo#601 S1: parks a turn-in at once for a structural reason (TravelTarget::ParkTurnInNow). True when parked.
// AiPlayerbot.QuestFirstProgression.AbandonStructuralParked (OB-10 answer 3): the second structural park in a row for
// a reason that cannot heal by itself (no taker destination, other continent below MinLevelForCrossMapQuestRoute -
// never a zone level, never after any other park in between) abandons the quest, [QuestAbandon]; a class quest is
// kept. The abandoned quest is marked so it is not taken again (no hourly accept / park / abandon cycle). Off: the
// quest stays in the log, parked.
bool ParkStructuralTurnIn(PlayerbotAI* ai, TravelTarget* travelTarget, uint32 questId, char const* reason, bool abandonable)
{
    bool abandonDue = false;
    if (!travelTarget->ParkTurnInNow(questId, reason, abandonable, abandonDue))
        return false;

    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    // twow-repo#601 R01 v4 (S1Cohort): only this mark makes the other AbandonStructuralParked sites act
    if (ai::quest_failure_class::AbandonParkedTurnIn(sPlayerbotAIConfig.questFirstProgressionAbandonStructuralParked &&
            sPlayerbotAIConfig.S1For(ai->GetBot()->GetGUIDLow()), abandonDue,
            !quest || quest->GetRequiredClasses()))
    {
        Player* bot = ai->GetBot();
        sLog.outBasic("[QuestAbandon] quest=%u reason=%s bot=%u level=%u parks=%u", questId, reason, bot->GetGUIDLow(),
            bot->GetLevel(), uint32(ai::turnin_park::AbandonAfterStructuralParks));
        ai->DropQuest(questId, "structural_parked_twice");
        travelTarget->NoteStructuralAbandon(questId);
    }
    return true;
}

// twow-repo#601 S1 (FailureClasses, OB-10 answer 3): the bot's open turn-ins are checked before a request may become
// turn-in-only. One GetDestinations call for all of them (one walk over the taker keys, bounded by the quest log); per
// taker destination its point closest to the bot is judged like a SetBestTarget candidate, in the main loop's order:
// turn-in route or death suppression, IsActive, the battleground quest skip, the enemy home zone, the route danger
// rule, the danger map. Any routable point validates the quest; a point the main loop would turn down before the route
// danger rule (or for the danger map) is "not now"; only structural answers (no point at all, other continent below
// MinLevelForCrossMapQuestRoute, a zone clearly above the bot) park it at once - so a structural class follows the
// counters the main loop would write. Writes the checked / validated lists the grind yield reads; one
// [QuestTakerCheck] line per request.
std::set<uint32> PreValidateTurnIns(PlayerbotAI* ai, Player* bot, TravelTarget* travelTarget)
{
    using namespace ai::quest_failure_class;
    // OB-10 18.10. (AiPlayerbot.BotUpdateTrace): the whole call into the [PreValidate] minute line and the trail note
    // "TIME:prevalidate <us> n=<open> ok=<validated>". Off: no clock read, nothing recorded.
    bool const tracePrevalidate = sPlayerbotAIConfig.botUpdateTrace;
    std::uint64_t const prevalidateStartUs = tracePrevalidate ? ai::bot_update::NowUs() : 0;
    AiObjectContext* context = ai->GetAiObjectContext();
    std::set<uint32> validated;
    std::vector<int32> openTurnIns;
    for (auto const& [questId, status] : bot->getQuestStatusMap())
        if (!status.m_rewarded && status.m_status == QUEST_STATUS_COMPLETE && travelTarget->IsTurnInOpen(questId))
            openTurnIns.push_back(int32(questId));

    std::string checkedList, validatedList;
    uint32 structural = 0, notNow = 0;
    if (!openTurnIns.empty())
    {
        PlayerTravelInfo const info(bot);
        WorldPosition const botPos(bot);
        std::map<uint32, std::vector<TravelDestination*>> takersByQuest;
        for (TravelDestination* destination : sTravelMgr.GetDestinations(info, uint32(TravelDestinationPurpose::QuestTaker),
                openTurnIns, true, 1000000.0f))
            if (QuestTravelDestination const* taker = dynamic_cast<QuestTravelDestination const*>(destination))
                takersByQuest[taker->GetQuestId()].push_back(destination);

        for (int32 const quest : openTurnIns)
        {
            uint32 const questId = uint32(quest);
            bool routable = false, notNowPoint = false, crossMap = false, zoneLevel = false;
            // twow-repo#587 (Z3): the main loop skips a battleground quest right after IsActive - not now, never parked.
            Quest const* const questTemplate = sObjectMgr.GetQuestTemplate(questId);
            bool const battlegroundQuest = sPlayerbotAIConfig.zoneExitSkipBattlegroundQuests && questTemplate &&
                ai::zone_exit::IsBattlegroundQuestSort(questTemplate->GetZoneOrSort());
            for (TravelDestination* destination : takersByQuest[questId])
            {
                WorldPosition* point = destination->GetClosestPoint(botPos);
                if (!point)
                    continue;
                if (travelTarget->IsDestinationDeathSuppressed(destination) || travelTarget->IsTurnInRouteSuppressed(destination, point))
                {
                    notNowPoint = true;
                    continue;
                }
                // The main loop's order: IsActive, battleground quest, enemy home zone - all before the route danger rule.
                if (!destination->IsActive(bot, info) || battlegroundQuest || point->isEnemyHomeZoneFor(bot->GetTeam()))
                {
                    notNowPoint = true;
                    continue;
                }
                route_danger::Reason const danger = TakerRouteDanger(ai, bot, point);
                if (danger == route_danger::Reason::CrossMap)
                {
                    crossMap = true;
                    continue;
                }
                if (danger == route_danger::Reason::TargetZoneLevel)
                {
                    zoneLevel = true;
                    continue;
                }
                // #307 danger map (the main loop's last deferral, same query): a death cluster on the way ages out.
                if (sPlayerbotAIConfig.dangerMapEnabled && point->getMapId() == bot->GetMapId() &&
                    danger_map::Instance().Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), point->getX(),
                        point->getY(), bot->GetLevel(), uint32(time(nullptr)), DangerMapParams(bot)).cells)
                {
                    notNowPoint = true;
                    continue;
                }
                routable = true;
                break;
            }

            TakerCheck const check = FoldTakerCheck(routable, notNowPoint, crossMap, zoneLevel);
            ListAdd(checkedList, questId);
            if (check == TakerCheck::Routable)
            {
                validated.insert(questId);
                ListAdd(validatedList, questId);
            }
            else if (IsStructural(check))
            {
                ++structural;
                ParkStructuralTurnIn(ai, travelTarget, questId, TakerCheckName(check), AbandonableReason(check));
            }
            else
                ++notNow;
        }

        sLog.outBasic("[QuestTakerCheck] bot=%u level=%u open=%u routable=%u structural=%u not_now=%u",
            bot->GetGUIDLow(), bot->GetLevel(), uint32(openTurnIns.size()), uint32(validated.size()), structural, notNow);
    }

    SET_AI_VALUE2(std::string, "manual string", CheckedTurnInsValue, checkedList);
    SET_AI_VALUE2(std::string, "manual string", ValidatedTurnInsValue, validatedList);
    if (tracePrevalidate)
    {
        std::uint64_t const us = ai::bot_update::SinceUs(prevalidateStartUs);
        ai::bot_update::Prevalidate().Add(us, uint32(openTurnIns.size()), uint32(validated.size()));
        if (ai::Engine* const traceEngine = ai->GetCurrentEngine())
            traceEngine->TraceNote("TIME:prevalidate %lluus n=%u ok=%u", (unsigned long long)us, uint32(openTurnIns.size()),
                uint32(validated.size()));
    }
    return validated;
}

// twow-repo#601 S1: the offered turn-ins the destination job found no taker point for (no spawn, a script-only game
// object, an excluded zone - the global quest giver fallback then filled the list). One walk over the list.
std::vector<uint32> TakerlessTurnIns(PartitionedTravelList const& list, std::vector<uint32> const& offered)
{
    std::vector<uint32> takerless;
    if (offered.empty())
        return takerless;

    std::set<uint32> withTaker;
    for (auto const& [partition, points] : list)
        for (auto const& [destination, position, distance] : points)
            if (destination && destination->GetPurpose() == TravelDestinationPurpose::QuestTaker)
                if (QuestTravelDestination const* taker = dynamic_cast<QuestTravelDestination const*>(destination))
                    withTaker.insert(taker->GetQuestId());

    for (uint32 const questId : offered)
        if (!withTaker.count(questId))
            takerless.push_back(questId);
    return takerless;
}

}

inline std::string GetTravelPurposeName(std::string purpose)
{
    if (Qualified::isValidNumberString(purpose) && TravelDestinationPurposeName.find(TravelDestinationPurpose(stoi(purpose))) != TravelDestinationPurposeName.end())
        return TravelDestinationPurposeName.at(TravelDestinationPurpose(stoi(purpose)));

    if (purpose.empty())
        return "quest";

    return purpose;
}

bool ChooseTravelTargetAction::Execute(Event& event)
{
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    if(travelTarget->GetStatus() != TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);
    FutureDestinations* futureDestinations = AI_VALUE(FutureDestinations*, "future travel destinations");
    std::string futureTravelPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    std::string futureTravelPurposeName = GetTravelPurposeName(futureTravelPurpose);
    uint32 targetRelevance = AI_VALUE2(int, "manual int", "future travel relevance");

    if (!futureDestinations->valid())
    {
        if (futureTravelPurpose == "quest")
            TraceQuestRouteRejection(bot, AI_VALUE2(std::string, "manual string", "future travel condition"), 0,
                "future_destination_invalid");
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
        context->ClearValues("no active travel destinations");        
        return false;
    }

    if (futureDestinations->wait_for(std::chrono::seconds(0)) == std::future_status::timeout)
        return false;

    PartitionedTravelList destinationList = futureDestinations->get();

    travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);

    ai->TellDebug(ai->GetMaster(), "Got " + std::to_string(destinationList.size()) + " new destination ranges for " + futureTravelPurposeName, "debug travel");

    TravelTarget newTarget = TravelTarget(ai);

    if (futureTravelPurpose == "pvp")
        newTarget.SetForced(true);

    if (AI_VALUE2(std::string, "manual string", "future travel condition") == "should travel named::guild meeting")
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 199u));
    }
    else if (AI_VALUE2(std::string, "manual string", "future travel condition") == "should travel named::guild order")
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 198u));
    }
    // Hotfix 9.2 (twow-repo#485, RosterGuild.FoundTravel): a quest-first roster bot dropped its trip to
    // a guild master within seconds (local quest hub filter, local quest preference) and asked for a
    // quest route again: v41 founders with 9 signatures restarted the turn-in trip every 10 min and
    // never arrived. Forced like a guild meeting: no quest filters on the way, it ends after an hour
    // (SetExpireIn) or when its condition (can hand in / may buy a charter) turns false.
    else if (futureTravelPurpose == "petition" && sPlayerbotAIConfig.rosterGuildFoundTravel && RosterGuildPlan::UsesRosterPath(ai))
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 197u));
    }
    else
    {
        newTarget.SetRelevance(targetRelevance);
    }

    // twow-repo#601 S2 R3 (GrindMode 1, review C-F2): the gap grind choice - only this one gets the R3 window snapshot and the
    // own-map skip in SetBestTarget; a quest choice of grind mobs keeps the old tests. GrindMode 0 / 2: false, as before.
    if (ai->IsQuestGapGrind() && futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)))
        newTarget.SetGapGrindChoice(true);

    if (!SetBestTarget(requester, &newTarget, destinationList, true, travelTarget))
    {
        // #416: out of time, not out of targets - the next list goes on.
        if (chooseBudgetExceeded)
            return false;

        SET_AI_VALUE2(bool, "no active travel destinations", futureTravelPurpose, true);
        ai->TellDebug(ai->GetMaster(), "No target set", "debug travel");

        // twow-repo#594 / P3-b: empty Station / Dungeon / Raid requests for [DirectTarget] state=summary.
        if (ai::direct_targets::AnyBuilt())
            ai::direct_targets::NoteEmpty(ai, futureTravelPurpose);

        if (futureTravelPurpose == "quest")
        {
            // #421 A: which filters took every candidate.
            TraceQuestRouteRejection(bot, AI_VALUE2(std::string, "manual string", "future travel condition"),
                uint32(destinationList.size()), "no_active_route_candidate", lastRejects.Format());
            // twow-repo#601 S5a (BandLadder): the stage this empty search ran at (read before widening; 0 when the switch is off).
            uint32 const bandSearchedStage = sPlayerbotAIConfig.questFirstProgressionBandLadder ?
                uint32(std::max(0, AI_VALUE2(int, "manual int", "quest search stage"))) : 0u;
            WidenQuestSearch(uint32(destinationList.size()));

            // twow-repo#594 / P3-a (AiPlayerbot.DirectTargets.Enable): a fresh bot (level <= FreshMaxLevel) gets the short
            // backoff below; a Z1b rescue requested in this pass wins over it.
            bool const freshBot = UsesQuestFirstProgression(bot) &&
                ai::quest_search::FreshBot(sPlayerbotAIConfig.directTargetsEnabled, bot->GetLevel(), sPlayerbotAIConfig.directTargetsFreshMaxLevel);
            bool rescueRequested = false;

            // twow-repo#587 (Z1b, AiPlayerbot.ZoneExit.StrandedRescueAfter): only other-continent candidates, search after
            // search - the bot asks for the (continent-correct) quest rescue instead of grinding until the idle limit.
            if (sPlayerbotAIConfig.zoneExitStrandedRescueAfter && UsesQuestFirstProgression(bot))
            {
                uint32 const otherRejects = lastRejects.hubFilter + lastRejects.zoneLevel + lastRejects.dangerMap + lastRejects.notActive +
                    lastRejects.enemyZone + lastRejects.knownInactive + lastRejects.deathSuppressed + lastRejects.turnInSuppressed;
                uint32 const stranded = ai::zone_exit::NextStrandedCount(
                    uint32(std::max(0, AI_VALUE2(int, "manual int", "zone exit stranded"))), ai::zone_exit::OnlyOtherContinent(lastRejects.crossMap, otherRejects));
                if (ai::zone_exit::StrandedRescueDue(stranded, sPlayerbotAIConfig.zoneExitStrandedRescueAfter))
                {
                    sLog.outBasic("[ZoneExit] state=stranded bot=%u level=%u map=%u zone=%u searches=%u cross_map=%u", bot->GetGUIDLow(),
                        bot->GetLevel(), bot->GetMapId(), bot->GetZoneId(), stranded, lastRejects.crossMap);
                    ai->RequestQuestRescue(uint32(time(nullptr)));
                    rescueRequested = true;
                    SET_AI_VALUE2(int, "manual int", "zone exit stranded", 0);
                }
                else
                    SET_AI_VALUE2(int, "manual int", "zone exit stranded", int(stranded));
            }

            // twow-repo#601 S1 (AiPlayerbot.QuestFirstProgression.FailureClasses, OB-10 answer 1): the class of this
            // rejection from its reject counts and the turn-ins it offered takers for (read here, NoteNoRouteTurnIns
            // below clears the list). Transient: the backoff only; structural: the turn-in is parked below; empty
            // offer: counted as before. Off (or a bot led by a real player): FailureClass::None, everything as before.
            ai::quest_failure_class::Decision failureDecision;
            std::vector<uint32> offeredTurnIns, takerlessTurnIns;
            int failuresBefore = 0;
            uint32 transientStreak = 0;
            bool transientCounts = false;
            if (sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) &&
                UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster())
            {
                for (std::string const& questId : split(AI_VALUE2(std::string, "manual string", HandInQuestsValue), ','))
                    if (uint32 const id = uint32(atoi(questId.c_str())))
                        offeredTurnIns.push_back(id);
                takerlessTurnIns = TakerlessTurnIns(destinationList, offeredTurnIns);
                failureDecision = ai::quest_failure_class::Classify(true, lastRejects, uint32(offeredTurnIns.size()),
                    uint32(takerlessTurnIns.size()));
                // twow-repo#627 S7 hook (FailureClasses.GroupNeeded): an empty offer while every open quest waits for S7 help is
                // group_needed (no grind-fallback count, no park); one queue lookup per open group quest, only with the key.
                if (sPlayerbotAIConfig.questFirstProgressionFailureClassesGroupNeeded &&
                    failureDecision.failureClass == ai::quest_failure_class::FailureClass::EmptyOffer)
                {
                    uint32 queuedGroupQuests = 0, otherOpenQuests = 0;
                    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                    {
                        uint32 const questId = bot->GetQuestSlotQuestId(slot);
                        if (!questId || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                            continue;
                        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                        if (quest && ai::quest_groups::IsGroupHelpQuest(quest->GetType(), quest->GetSuggestedPlayers(),
                                sPlayerbotAIConfig.questGroupsSuggestedPlayers) && ai::quest_groups::QueuedAge(bot->GetGUIDLow(), questId))
                            ++queuedGroupQuests;
                        else
                            ++otherOpenQuests;
                    }
                    failureDecision.failureClass = ai::quest_failure_class::WithGroupNeeded(failureDecision.failureClass, true,
                        queuedGroupQuests, otherOpenQuests);
                }
                // The counter before this rejection (the class line logs whether it really grew) and the transient
                // rejections in a row (TransientCountsAfter: 0 = never counted; the N-th in a row counts once).
                failuresBefore = std::max(0, AI_VALUE2(int, "manual int", "quest route failures"));
                if (failureDecision.failureClass == ai::quest_failure_class::FailureClass::Transient)
                    transientStreak = uint32(std::max(0, AI_VALUE2(int, "manual int", ai::quest_failure_class::TransientStreakValue))) + 1;
                transientCounts = ai::quest_failure_class::TransientStreakCounts(transientStreak,
                    sPlayerbotAIConfig.questFirstProgressionTransientCountsAfter);
                SET_AI_VALUE2(int, "manual int", ai::quest_failure_class::TransientStreakValue, transientCounts ? 0 : int(transientStreak));
            }

            // twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder): the zone is done - step to the next hub. Decided
            // before the 8.1 failure count and backoff (a step counts nothing); a Z1b rescue and P3-a fresh bots win.
            // F1 (BandStepAfterEmptySearches): every failed search counts into the band streak, whatever its class; no park
            // resets it - only a found route (below) or the band step itself. F4 (BandWalkAfterEmptySearches) reads the same streak.
            if (sPlayerbotAIConfig.questFirstProgressionBandLadder && sPlayerbotAIConfig.BandStreakOn())
                SET_AI_VALUE2(int, "manual int", ai::quest_band::BandEmptyStreakValue,
                    std::min(int(ai::quest_band::MaxStreakSteps), std::max(0, AI_VALUE2(int, "manual int", ai::quest_band::BandEmptyStreakValue)) + 1));
            // F4: a due walk's rung (no step); started at the end of this branch, once the backoff and the parks are settled.
            ai::quest_band::WalkPlan bandWalk;
            if (sPlayerbotAIConfig.questFirstProgressionBandLadder &&
                TryBandStep(requester, travelTarget, bandSearchedStage, freshBot, rescueRequested,
                    sPlayerbotAIConfig.questFirstProgressionBandWalkAfterEmptySearches ? &bandWalk : nullptr))
            {
                if (sPlayerbotAIConfig.BandStreakOn())
                    SET_AI_VALUE2(int, "manual int", ai::quest_band::BandEmptyStreakValue, 0);   // F1: a step ends the streak
                return true;
            }

            // Hotfix 8.1: wait before the next quest route request (2, 4, 8, then 10 min).
            if (UsesQuestFirstProgression(bot))
            {
                uint32 const failures = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route failures"))) + 1;
                // twow-repo#601 S1: a transient rejection waits the same backoff, but the grind-fallback counter does not grow
                // (unless TransientCountsAfter = N and this is the N-th transient rejection in a row).
                if (ai::quest_failure_class::CountsTowardGrind(failureDecision.failureClass) || transientCounts)
                    SET_AI_VALUE2(int, "manual int", "quest route failures", int(failures));
                // twow-repo#601 (GrindFallbackMinutes): the time of the last counted empty search, under the same condition.
                if (ai::quest_failure_class::CountsTowardGrind(failureDecision.failureClass) || transientCounts)
                    SET_AI_VALUE2(int, "manual int", "quest route failed at", int(uint32(time(nullptr))));
                // twow-repo#594 / P3-a: one formula - a fresh bot without a rescue in this pass waits FreshBackoffSeconds.
                bool const shortBackoff = freshBot && !rescueRequested && sPlayerbotAIConfig.directTargetsFreshBackoffSeconds;
                uint32 const backoff = shortBackoff ?
                    ai::quest_search::RouteBackoffSeconds(failures, true, sPlayerbotAIConfig.directTargetsFreshBackoffSeconds) :
                    ai::quest_search::RouteBackoffSeconds(failures);
                SET_AI_VALUE2(int, "manual int", "quest route backoff until", int(uint32(time(nullptr)) + backoff));
            }

            // twow-repo#601 S1: a structural failure parks the turn-in at once for TurnInParkSeconds - every offered one
            // when all rejects were structural, else the ones the job found no taker point for. Like a #485 park below,
            // a park ends the backoff and resets the grind-fallback counter, so the failure never counts toward grinding.
            uint32 structuralParks = 0;
            if (failureDecision.parkTakerless || failureDecision.parkAllOffered)
                for (uint32 const questId : offeredTurnIns)
                {
                    bool const takerless = std::find(takerlessTurnIns.begin(), takerlessTurnIns.end(), questId) != takerlessTurnIns.end();
                    if (!takerless && !failureDecision.parkAllOffered)
                        continue;
                    if (ParkStructuralTurnIn(ai, travelTarget, questId,
                            takerless ? ai::quest_failure_class::TakerCheckName(ai::quest_failure_class::TakerCheck::NoDestination) :
                                failureDecision.reason,
                            takerless || failureDecision.abandonable))
                        ++structuralParks;
                }
            if (structuralParks)
            {
                SET_AI_VALUE2(bool, "no active travel destinations", futureTravelPurpose, false);
                SET_AI_VALUE2(int, "manual int", "quest route failures", 0);
                SET_AI_VALUE2(int, "manual int", "quest route backoff until", 0);
            }

            // twow-repo#485: no way to any offered taker - unless not every taker was judged (a stale
            // list, a random range skip, candidates a resumed choice skipped) or a filter that lifts
            // by itself took one: a suppressed turn-in route (counted already) or, unless
            // TurnInParkCountsRouteDanger, a route danger deferral (another map, a zone above the bot,
            // a death cluster). The local quest hub filter is no such case: a retry hides the same
            // takers again, so the hub taker's own reason decides. A turn-in parked here no longer
            // holds the next request back, so that one need not wait out the backoff.
            if (NoteNoRouteTurnIns(context, travelTarget, ai::turnin_park::CountsAsNoRoute(ai::turnin_park::RouteOutcome::NoTarget,
                chooseBudgetExceeded, lastRejects.crossMap + lastRejects.zoneLevel + lastRejects.dangerMap, lastRejects.turnInSuppressed,
                lastRejects.movedAway + lastRejects.rangeSkip + lastRejects.resumeSkipped,
                sPlayerbotAIConfig.questFirstProgressionTurnInParkCountsRouteDanger)))
            {
                SET_AI_VALUE2(bool, "no active travel destinations", futureTravelPurpose, false);
                SET_AI_VALUE2(int, "manual int", "quest route failures", 0);
                SET_AI_VALUE2(int, "manual int", "quest route backoff until", 0);
            }

            // twow-repo#601 S1: one line per classified rejection (bounded by the route backoff). counted = the counter
            // grew by this rejection and no park (structural or #485) reset it; streak = transient rejections in a row.
            if (failureDecision.failureClass != ai::quest_failure_class::FailureClass::None)
            {
                int const failuresAfter = AI_VALUE2(int, "manual int", "quest route failures");
                sLog.outBasic("[QuestFailureClass] bot=%u level=%u class=%s offered=%u takerless=%u parked=%u counted=%u streak=%u failures_before=%d failures=%d",
                    bot->GetGUIDLow(), bot->GetLevel(), ai::quest_failure_class::ClassName(failureDecision.failureClass),
                    uint32(offeredTurnIns.size()), uint32(takerlessTurnIns.size()), structuralParks,
                    failuresAfter == failuresBefore + 1 ? 1u : 0u, transientStreak, failuresBefore, failuresAfter);
                // GrindMode 2: the class goes on this idle episode's [QuestIdle] lines (OB-10: measure transient chains).
                if (ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))
                    ai->NoteQuestIdleClass(uint8(failureDecision.failureClass));
            }

            // twow-repo#594 / P3: once at the end of the branch, after Z1b, the backoff and the #485 park have seen the
            // rejection - the counts for [DirectTarget] state=summary.
            ai::quest_search::DirectTargetCounters& directTargets = ai::quest_search::DirectTargetStats();
            if (freshBot)
            {
                directTargets.freshRejections.fetch_add(1, std::memory_order_relaxed);
                if (rescueRequested)
                    directTargets.rescueSkips.fetch_add(1, std::memory_order_relaxed);
                else if (sPlayerbotAIConfig.directTargetsFreshBackoffSeconds)
                    directTargets.freshBackoffs.fetch_add(1, std::memory_order_relaxed);
            }
            if (sPlayerbotAIConfig.directTargetsTraceInactive)
                for (uint32 reason = 0; reason < ai::quest_search::RejectCounts::InactiveReasons; ++reason)
                    directTargets.inactive[reason].fetch_add(lastRejects.notActiveReason[reason], std::memory_order_relaxed);

            // twow-repo#601 S5a F4 (BandWalkAfterEmptySearches, owner D1 "rumstehen ist keine Option"): no step, but the backoff
            // stands - the bot walks toward the rung the step would take until the backoff ends. A park above ended the backoff:
            // no walk, the next search comes at once.
            if (bandWalk.rung >= 0 &&
                StartBandWalk(requester, travelTarget, bandWalk, uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until")))))
                return true;
        }
        // twow-repo#601 S1 (OB-10 answer 5): the grind search of a quest-first bot found nothing - it asks for a quest
        // route again once its backoff ends (the L6-8 escape). A pure log line: FailureClasses or TraceTravelDecisions.
        else if (futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)) && UsesQuestFirstProgression(bot) &&
            (sPlayerbotAIConfig.questFirstProgressionFailureClasses || sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions))
            sLog.outBasic("[QuestSearch] state=grind_none bot=%u level=%u failures=%d ranges=%u moved_away=%u",
                bot->GetGUIDLow(), bot->GetLevel(), AI_VALUE2(int, "manual int", "quest route failures"),
                uint32(destinationList.size()), lastRejects.movedAway);

        // twow-repo#601 S2 / S4 [GrindWindow] no_dest= (GrindMode 1, OB-10 rev. 2 point 2): the gap filler's grind fetch found no
        // destination in the window on its own map (R3) - the bot stands until the backoff ends. Once per window (backoff),
        // never after a trip of that window started. GrindMode 0 / 2: IsQuestGapGrind() is false, nothing read.
        if (ai->IsQuestGapGrind() && futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)))
        {
            int const backoffUntil = std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until"));
            if (ai::quest_grind_mode::GapNoDestDue(uint32(time(nullptr)) < uint32(backoffUntil), backoffUntil,
                    AI_VALUE2(int, "manual int", "gap window logged"), AI_VALUE2(int, "manual int", "gap window empty")))
            {
                SET_AI_VALUE2(int, "manual int", "gap window empty", backoffUntil);
                PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::NoDest));
            }
        }

        return false;
    }

    if (futureTravelPurpose == "quest")
    {
        TraceQuestRouteDecision(bot, newTarget, AI_VALUE2(std::string, "manual string", "future travel condition"),
            "travelmgr_route_validated");

        // Hotfix 8.1: a route was found - no more backoff.
        SET_AI_VALUE2(int, "manual int", "quest route failures", 0);
        SET_AI_VALUE2(int, "manual int", "quest route backoff until", 0);
        // twow-repo#601 S5a F1 / F4: a route was found - the band streak ends.
        if (sPlayerbotAIConfig.BandStreakOn())
            SET_AI_VALUE2(int, "manual int", ai::quest_band::BandEmptyStreakValue, 0);
        // twow-repo#601 S1 (FailureClasses): a route was found - the transient streak ends.
        if (sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()))
            SET_AI_VALUE2(int, "manual int", ai::quest_failure_class::TransientStreakValue, 0);
        // twow-repo#601 GrindMode 2: a quest target ends the idle episode - the next idle reason gets its line again.
        if (ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))
            ai->ClearQuestIdle();
        // twow-repo#601 QuestFirstOrder: a quest target ends a "no quest" episode (state=quest_found seconds=; nothing without one).
        ai->EndQuestOrderEpisode();

        // #421 B: the widened search found something.
        int const stage = AI_VALUE2(int, "manual int", "quest search stage");
        if (stage > 0 && UsesQuestFirstProgression(bot))
            sLog.outBasic("[QuestSearch] state=found bot=%u level=%u stage=%d distance=%.0f",
                bot->GetGUIDLow(), bot->GetLevel(), stage, newTarget.Distance(bot));

        // twow-repo#485: another target than a taker is the quest giver fallback - the job found
        // no taker at all. Same inputs as in the no-target branch.
        NoteNoRouteTurnIns(context, travelTarget, ai::turnin_park::CountsAsNoRoute(
            newTarget.GetDestination()->GetPurpose() == TravelDestinationPurpose::QuestTaker ?
                ai::turnin_park::RouteOutcome::Taker : ai::turnin_park::RouteOutcome::Fallback,
            chooseBudgetExceeded, lastRejects.crossMap + lastRejects.zoneLevel + lastRejects.dangerMap, lastRejects.turnInSuppressed,
            lastRejects.movedAway + lastRejects.rangeSkip + lastRejects.resumeSkipped,
            sPlayerbotAIConfig.questFirstProgressionTurnInParkCountsRouteDanger));
    }
    else if (futureTravelPurpose == std::to_string(uint32(TravelDestinationPurpose::Grind)) && UsesQuestFirstProgression(bot))
    {
        // twow-repo#601 GrindMode 2 (owner 17.10.2026): a pure grind fetch is never taken as the travel target (the
        // request is already refused; this catches a fetch that was under way). The bot stands idle, [QuestIdle].
        if (ai->IsQuestNoGrind())
        {
            int32 const grindEntry = newTarget.GetEntry();
            ai->NoteQuestIdle(uint8(ai::quest_grind_mode::RequestReason(ai::quest_grind_mode::IsMoneyCondition(
                AI_VALUE2(std::string, "manual string", "future travel condition")))),
                ai::quest_grind_mode::TargetText("grind_destination", grindEntry > 0 ? uint32(grindEntry) : 0u));
            return false;
        }
        // twow-repo#601 decision order (QuestFirstOrder, owner 20.10.2026 "es gibt immer eine Quest"; CLI-Deep review): the 8.32
        // grind fallback and the money grind are no quest work - the grind target filter would refuse the kills there anyway.
        // Refused like GrindMode 2 (a fetch under way); the bot stands visibly, grind_refused= / no_quest=.
        if (ai->IsQuestFirstOrder())
        {
            PlayerbotAI::NoteQuestOrder(uint8(ai::quest_first_order::Event::GrindRefused));
            ai->NoteQuestOrderEpisode(uint8(ai::quest_first_order::Event::GrindRefused));
            return false;
        }
        // twow-repo#601 S2 / S4 (GrindMode 1, OB-10 rev. 2): a gap grind trip. R1 again (the fetch ran async: a backoff that
        // ended meanwhile takes no grind target, the quest request comes next); R2: the trip's window ends with the backoff
        // (remaining, at most RouteBackoffMaxSeconds, + 30 s; READY / TRAVEL / WORK), and its expiry is no failure. One
        // state=grind_window line per window (backoff); state=grind_fallback stays GrindMode 0. GrindMode 0 / 2: as before.
        if (ai->IsQuestGapGrind())
        {
            uint32 const now = uint32(time(nullptr));
            int const backoffUntil = std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until"));
            if (!ai::quest_grind_mode::GapWindowOpen(now < uint32(backoffUntil), false))
                return false;
            newTarget.SetGapWindowEnd(now + ai::quest_grind_mode::GapWindowSeconds(uint32(backoffUntil), now,
                ai::quest_search::RouteBackoffMaxSeconds));
            if (backoffUntil != AI_VALUE2(int, "manual int", "gap window logged"))
            {
                SET_AI_VALUE2(int, "manual int", "gap window logged", backoffUntil);
                PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Starts));
                uint32 const level = bot->GetLevel();
                sLog.outBasic("%s", ai::quest_grind_mode::FormatGapWindowLine(bot->GetGUIDLow(), level, uint32(backoffUntil) - now,
                    ai::quest_grind_mode::GapWindowLowLevel(level, MaNGOS::XP::GetGrayLevel(level)),
                    ai::quest_grind_mode::GapWindowHighLevel(level), bot->GetMapId()).c_str());
            }
        }
        else
        {
            // Hotfix 8.32 (twow-repo#544): a quest-first bot grinds after empty quest route searches.
            sLog.outBasic("[QuestSearch] state=grind_fallback bot=%u level=%u failures=%d distance=%.0f",
                bot->GetGUIDLow(), bot->GetLevel(), AI_VALUE2(int, "manual int", "quest route failures"), newTarget.Distance(bot));
        }
        // twow-repo#594 / P3-a: grind targets of fresh bots for [DirectTarget] state=summary.
        if (ai::quest_search::FreshBot(sPlayerbotAIConfig.directTargetsEnabled, bot->GetLevel(), sPlayerbotAIConfig.directTargetsFreshMaxLevel))
            ai::quest_search::DirectTargetStats().freshGrind.fetch_add(1, std::memory_order_relaxed);
    }

    // twow-repo#594 / P3-b: [DirectTarget] state=pick, the class cooldown, and for a shore point the "fish spot".
    if (ai::direct_targets::AnyBuilt())
        ai::direct_targets::OnChoice(ai, newTarget);

    setNewTarget(requester, &newTarget, travelTarget);
    
    return true;
}

void ChooseTravelTargetAction::WidenQuestSearch(uint32 ranges)
{
    if (!UsesQuestFirstProgression(bot))
        return;

    uint32 const now = uint32(time(nullptr));
    uint32 const stage = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest search stage")));
    uint32 const lastWiden = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest search widen time")));
    uint32 const next = ai::quest_search::NextStage(stage, lastWiden, now);
    if (next == stage)
        return;

    SET_AI_VALUE2(int, "manual int", "quest search stage", int(next));
    SET_AI_VALUE2(int, "manual int", "quest search widen time", int(now));
    sLog.outBasic("[QuestSearch] state=widen bot=%u level=%u stage=%u radius=%.0f zone=%u ranges=%u",
        bot->GetGUIDLow(), bot->GetLevel(), next, ai::quest_search::GiverRadius(bot->GetLevel(), next),
        bot->GetZoneId(), ranges);
}

bool ChooseTravelTargetAction::isUseful()
{
    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (AI_VALUE(bool, "travel target active"))
        return false;

    // twow-repo#540: a roster bot on its own chooses its first journey 0..JitterSeconds after login
    // (by guid), so a server start does not send 180 bots onto long journeys at once. Also holds
    // BotBrain intents, whose action inherits this check.
    if (sPlayerbotAIConfig.startupTravelJitterSeconds && !ai->HasRealPlayerMaster() &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        !startup_travel::JitterDone(uint64(time(nullptr)), uint64(bot->GetLoginTime()), bot->GetGUIDLow(),
            sPlayerbotAIConfig.startupTravelJitterSeconds))
    {
        startup_travel::SharedCounters().jitter.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    return true;
}

void ChooseTravelTargetAction::setNewTarget(Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)
{
    if (CanFreeMoveValue::CanFreeMoveTo(ai, newTarget->GetPosStr()))
        ReportTravelTarget(bot, requester, newTarget, oldTarget);

    //If we are heading to a creature/npc clear it from the ignore list. 
    if (oldTarget && oldTarget == newTarget && newTarget->GetEntry())
    {
        std::set<ObjectGuid>& ignoreList = context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Get();

        for (auto& i : ignoreList)
        {
            if (i.GetEntry() == newTarget->GetEntry())
            {
                ignoreList.erase(i);
            }
        }

        context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Set(ignoreList);
    }

    //Actually apply the new target to the travel target used by the bot.
    // twow-repo#485: refused (and the old target expired) for the taker of a parked turn-in.
    if (!oldTarget->CopyTarget(newTarget))
        return;

    if (oldTarget->IsForced()) //Make sure travel goes into cooldown after getting to the destination.
        oldTarget->SetExpireIn(HOUR * IN_MILLISECONDS);

    if(!AI_VALUE2(std::string, "manual string", "future travel condition").empty())
        AI_VALUE(TravelTarget*, "travel target")->SetConditions({ AI_VALUE2(std::string, "manual string", "future travel condition")});

    if (QuestObjectiveTravelDestination* dest = dynamic_cast<QuestObjectiveTravelDestination*>(oldTarget->GetDestination()))
    {
        std::string condition = "group or::{following party,need quest objective::{" + std::to_string(dest->GetQuestId()) + "," + std::to_string((uint8)dest->GetObjective()) + "}}";
        oldTarget->AddCondition(condition);
        if (Quest const* q = sObjectMgr.GetQuestTemplate(dest->GetQuestId()))
            sPlayerbotAIConfig.logEvent(ai, "QuestTravelToObjective", q->GetTitle(), std::to_string(dest->GetQuestId()));
    }
    else if (QuestRelationTravelDestination* dest = dynamic_cast<QuestRelationTravelDestination*>(oldTarget->GetDestination()))
    {
        std::string condition, qualifier = std::to_string(dest->GetEntry());
        if (dest->GetPurpose() == TravelDestinationPurpose::QuestGiver)

            condition = "group or::{following party,or::{can accept quest npc::" + qualifier + ",can accept quest low level npc::" + qualifier + "}}";
        else
            condition = "group or::{following party,can turn in quest npc::" + qualifier + "}";

        oldTarget->AddCondition(condition);
        if (Quest const* q = sObjectMgr.GetQuestTemplate(dest->GetQuestId()))
        {
            std::string eventName = (dest->GetPurpose() == TravelDestinationPurpose::QuestGiver) ? "QuestTravelToGiver" : "QuestTravelToTaker";
            sPlayerbotAIConfig.logEvent(ai, eventName, q->GetTitle(), std::to_string(dest->GetQuestId()));
        }
    }

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);

    //Clear rpg and attack/grind target. We want to travel, not hang around some more.
    RESET_AI_VALUE(GuidPosition,"rpg target");
    RESET_AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target");
    RESET_AI_VALUE(ObjectGuid,"attack target");
    RESET_AI_VALUE(bool, "travel target active");
    context->ClearValues("no active travel destinations");
    SET_AI_VALUE2(std::string, "manual string", "future travel detail", std::string());
};

//Tell the master what travel target we are moving towards.
//This should at some point be rewritten to be denser or perhaps logic moved to ->getTitle()
void ChooseTravelTargetAction::ReportTravelTarget(Player* bot, Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)
{
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    TravelDestination* destination = newTarget->GetDestination();

    TravelDestination* oldDestination;

    if (oldTarget)
        oldDestination = oldTarget->GetDestination();

    std::ostringstream out;

    if (newTarget->IsForced())
        out << "(Forced) ";
        
    std::string futureTravelPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    std::string futureTravelPurposeName = GetTravelPurposeName(futureTravelPurpose);

    std::string futureTravelCondition = AI_VALUE2(std::string, "manual string", "future travel condition");
    bool isGuildMeeting = futureTravelCondition == "should travel named::guild meeting";

    std::string futureTravelDetail = AI_VALUE2(std::string, "manual string", "future travel detail");

    std::string shortName = destination->GetShortName();    

    if (typeid(*destination) == typeid(NullTravelDestination))
    {
        out.clear();
        if (!oldDestination || typeid(*oldDestination) != typeid(NullTravelDestination))
            out << "Nowhere to travel. Idling a bit.";
    }
    else
    {
        if (newTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
        {
            out << "Currently";

            if (newTarget->GetPosition() && !newTarget->GetPosition()->getAreaName().empty())
            {
                if (destination->DistanceTo(bot) < 100.0f)
                    out << " in ";
                else
                    out << " near ";

                out << newTarget->GetPosition()->getAreaName();
            }
            else
                out << " traveling";
        }
        else
        {
            if (bot->GetGroup() && !ai->IsGroupLeader() && (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT)))
                out << "I want to travel";
            else if (newTarget->IsGroupCopy() && newTarget->GetGroupmember().GetPlayer())
                out << "Taking " << newTarget->GetGroupmember().GetPlayer()->GetName();
            else if (oldDestination && oldDestination == destination)
                out << "Continuing";
            else
                out << "Traveling";

            if (newTarget->GetPosition())
            {
                out << " " << round(newTarget->Distance(bot)) << "y";
                if (!newTarget->GetPosition()->getAreaName().empty())
                    out << " to " << newTarget->GetPosition()->getAreaName();
            }
        }

        if (shortName.find("quest") == 0)
        {
            QuestTravelDestination* QuestDestination = (QuestTravelDestination*)destination;
            out << " for " << QuestDestination->QuestTravelDestination::GetTitle();
            out << " to " << QuestDestination->GetTitle();
        }
        else if (shortName == "rpg")
        {
            out << " to " << destination->GetTitle();

            if (futureTravelPurpose == "city")
                out << " to hang around in the city";
            else if (futureTravelPurpose == "tabard")
                out << " to buy a tabard";
            else if (futureTravelPurpose == "petition")
                out << " to hand in a petition";
            else
                out << " to roleplay";
        }
        else
        {
            out << " to " << destination->GetTitle();
        }
    }

    if (newTarget->GetRetryCount(false))
        out << " (retry " << newTarget->GetRetryCount(false) << "/5)";
    if (out.str().empty())
        return;

    // #333: profession trips (fishing, mining, herbalism, skinning) are not
    // announced in party or raid chat; they stay visible with "debug travel".
    bool const gatherTrip = destination && (destination->GetPurpose() == TravelDestinationPurpose::GatherFishing ||
        destination->GetPurpose() == TravelDestinationPurpose::GatherMining ||
        destination->GetPurpose() == TravelDestinationPurpose::GatherHerbalism ||
        destination->GetPurpose() == TravelDestinationPurpose::GatherSkinning ||
        ai::direct_targets::IsSilentTrip(destination));  // twow-repo#594 / P3-b: stations, shore points, entrances

    if (gatherTrip)
        ai->TellDebug(requester, out.str(), "debug travel");
    else if (!isGuildMeeting)
        ai->TellPlayerNoFacing(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_TALK, false);

    if (!futureTravelDetail.empty())
        ai->TellDebug(requester, "Farming item: " + futureTravelDetail + " from " + destination->GetTitle(), "debug travel");

    std::string message = out.str().c_str();

    if (sPlayerbotAIConfig.hasLog("travel_map.csv"))
    {
        WorldPosition botPos(bot);
        WorldPosition destPos = *newTarget->GetPosition();

        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
        out << bot->GetName() << ",";
        out << std::fixed << std::setprecision(2);

        out << std::to_string(bot->getRace()) << ",";
        out << std::to_string(bot->getClass()) << ",";
        float subLevel = ai->GetLevelFloat();

        out << subLevel << ",";

        if (!destPos)
            destPos = botPos;

        botPos.printWKT({ botPos,destPos }, out, 1);

        if (typeid(*destination) == typeid(NullTravelDestination))
            out << "0,";
        else
            out << round(newTarget->GetDestination()->DistanceTo(botPos)) << ",";

        out << "new," << "\"" << destination->GetTitle() << "\",\"" << message << "\"";

        out << "," << futureTravelPurposeName;

        sPlayerbotAIConfig.log("travel_map.csv", out.str().c_str());        
    }
}

inline std::string PrintPartion(uint32 sqPartition)
{
    uint32 prevPartition = 0;
    for (auto& partition : travelPartitions)
    {
        if (sqrt(sqPartition) == partition)
            return std::to_string(prevPartition) + "-" + std::to_string(partition);

        prevPartition = partition;
    }

    return "> " + std::to_string(prevPartition);
}

//Sets the target to the best destination.
bool ChooseTravelTargetAction::SetBestTarget(Player* requester, TravelTarget* target, PartitionedTravelList& partitionedList, bool onlyActive,
    TravelTarget const* excludedTurnInTarget)
{
    bool distanceCheck = true;
    std::unordered_map<TravelDestination*, bool> isActive;

    // twow-repo#541 (audit A10, AiPlayerbot.Perf.TravelInfoReuse): one PlayerTravelInfo per choice, built on
    // first use, and an IsActive answer this choice already has is reused. Off: a new info per check.
    bool const travelInfoReuse = sPlayerbotAIConfig.perfTravelInfoReuse;
    std::optional<PlayerTravelInfo> choiceTravelInfo;
    auto const choiceInfo = [this, &choiceTravelInfo]() -> PlayerTravelInfo const&
    {
        if (!choiceTravelInfo)
            choiceTravelInfo.emplace(bot);
        return *choiceTravelInfo;
    };
    auto const reuseActive = [this, &isActive, &choiceInfo](TravelDestination* dest) -> bool
    {
        auto const known = isActive.find(dest);
        if (known != isActive.end())
            return known->second;
        return isActive[dest] = dest->IsActive(bot, choiceInfo());
    };
    // twow-repo#601 S2 R3 (GrindMode 1, review C-F2): the gap grind choice asks IsActive with the R3 window snapshot - one per
    // choice, the reuse path below; every other choice as before (the flag is set only behind IsQuestGapGrind).
    bool const gapGrindChoice = target->IsGapGrindChoice();
    if (gapGrindChoice)
        choiceTravelInfo.emplace(bot, true);

    bool hasTarget = false;
    TravelTarget const* persistentTarget = AI_VALUE(TravelTarget*, "travel target");

    // #416: a time budget per update. Candidates an aborted choice already
    // checked are skipped next time, so the search moves on.
    uint32 const chooseStart = WorldTimer::getMSTime();
    std::string const choosePurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    uint32 const resumeSkip = chooseResume.SkipFor(choosePurpose);
    uint32 candidateIndex = 0, checked = 0;
    chooseBudgetExceeded = false;
    lastRejects = ai::quest_search::RejectCounts();
    uint32 deferredCrossMap = 0, deferredZoneLevel = 0;
    uint32 deferredDeathCluster = 0, deathClusterCells = 0, deathClusterWorstKillerLevel = 0;
    // Re-deaths at camps (DestinationDeaths.CampBlock): skipped candidates, quest destinations ranked lower and the kept fallback.
    uint32 campSkipped = 0, campRankedLower = 0;
    TravelDestination* campFallbackDestination = nullptr;
    WorldPosition* campFallbackPosition = nullptr;
    bool const preferLocalQuest = UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster() &&
        AI_VALUE2(std::string, "manual string", "future travel purpose") == "quest";
    bool hasActiveLocalQuestHub = false;
    // twow-repo#601 S1 (AiPlayerbot.QuestFirstProgression.FailureClasses, OB-10 answer 4): the pre-loop below asks the
    // main loop's skip predicate (CandidateSkipOf) before it lets a local candidate switch the hub filter on, and no
    // random range skip happens while the hub filter is on. false = both as before.
    bool const failureClasses = preferLocalQuest && sPlayerbotAIConfig.questFirstProgressionFailureClasses &&
        sPlayerbotAIConfig.S1For(bot->GetGUIDLow());
    // twow-repo#601 S3b (AiPlayerbot.QuestFirstProgression.HubPreloopSameFilters, OB-10 rev. 2 Q-B2, only with
    // FailureClasses): the pre-loop also asks the main loop's post-IsActive deferrals (PostActiveDeferralOf) - no
    // counter, no trace there. false = the pre-loop as before.
    bool const hubPreloopSameFilters = failureClasses && sPlayerbotAIConfig.questFirstProgressionHubPreloopSameFilters &&
        sPlayerbotAIConfig.S3For(bot->GetGUIDLow());
    // twow-repo#601 GrindMode 2 (review): a forced target (the guild Farm / Kill order) never asks IsActive here, in
    // TravelTarget::CheckStatus or by status time, so the order's kill trips (drop or name-matched mobs, bosses,
    // skinning) would be a forced trip to mobs the grind target filter then refuses - the bot parked there while the
    // order holds. With GrindMode 2 a forced kill trip goes only to a quest objective mob. Not forced, or GrindMode 0 / 1:
    // false before anything else is read.
    bool const forcedNoGrind = target->IsForced() && ai->IsQuestNoGrindTrip();   // + QuestFirstOrder (#601 review)
    std::vector<int32> forcedNeeded;
    if (forcedNoGrind)
        forcedNeeded = AI_VALUE(std::vector<int32>, "needed quest entries");
    uint32 forcedKillSkipped = 0;
    int32 forcedKillEntry = 0;

    // A reachable point is not necessarily usable. Check the normal
    // destination activation predicate before holding the bot in its current
    // map, so an inactive local point cannot hide a valid follow-up elsewhere.
    if (preferLocalQuest && !target->IsForced() && !resumeSkip)
    {
        std::optional<PlayerTravelInfo> preLoopTravelInfo;
        PlayerTravelInfo const& travelInfo = travelInfoReuse ? choiceInfo() : preLoopTravelInfo.emplace(bot);
        for (auto const& [partition, travelPointList] : partitionedList)
        {
            for (auto const& [destination, position, distance] : travelPointList)
                if (ai::travel_choose::OverBudget(WorldTimer::getMSTimeDiffToNow(chooseStart)))
                    break;
                else if ((!failureClasses || CandidateSkipOf(ai, bot, target, persistentTarget, excludedTurnInTarget, destination,
                        position) == CandidateSkip::None) &&
                    IsLocalActiveQuestHubDestination(bot, destination, position, travelInfo) &&
                    (isActive[destination] = true))
                {
                    // twow-repo#601 S3b: an active local candidate the main loop defers after IsActive keeps the hub
                    // filter off; its true IsActive answer above stays known (the main loop defers it the same way).
                    danger_map::DangerMap::Hit preLoopHit;
                    if (hubPreloopSameFilters && PostActiveDeferralOf(ai, bot, target, destination, position, preLoopHit) !=
                            PostActiveDeferral::None)
                        continue;
                    hasActiveLocalQuestHub = true;
                    break;
                }
            // A10: one active local hub is enough; the old path walks the remaining partitions.
            if (travelInfoReuse && hasActiveLocalQuestHub)
                break;
        }
    }

    for (auto& [partition, travelPointList] : partitionedList)
    {
        ai->TellDebug(requester, "Found " + std::to_string(travelPointList.size()) + " points at range " + PrintPartion(partition), "debug travel");

        for (auto& [destination, position, distance] : travelPointList)
        {
            // #416: skip what an aborted choice already checked; stop when the time is up.
            // twow-repo#485: a skipped candidate is not judged by this choice (no_route rule).
            if (candidateIndex++ < resumeSkip)
            {
                ++lastRejects.resumeSkipped;
                continue;
            }
            if (checked++ && ai::travel_choose::OverBudget(WorldTimer::getMSTimeDiffToNow(chooseStart)))
            {
                chooseBudgetExceeded = true;
                chooseResume.Abort(choosePurpose, candidateIndex - 1);
                break;
            }

            // twow-repo#601 S1: the skips in one predicate (CandidateSkipOf, same order and counters as before) - the
            // local quest hub pre-loop asks the same one with FailureClasses.
            switch (CandidateSkipOf(ai, bot, target, persistentTarget, excludedTurnInTarget, destination, position))
            {
                case CandidateSkip::TurnInSuppressed:
                    ai->TellDebug(requester, "Skipping temporarily suppressed completed-quest turn-in route.", "debug travel");
                    ++lastRejects.turnInSuppressed;
                    continue;
                case CandidateSkip::DeathSuppressed:
                    ai->TellDebug(requester, "Skipping destination after repeated deaths there.", "debug travel");
                    ++lastRejects.deathSuppressed;
                    continue;
                case CandidateSkip::QuestLoopSuppressed:
                    ai->TellDebug(requester, "Skipping quest objective after repeated failures.", "debug travel");
                    ++lastRejects.questLoopSuppressed;
                    continue;
                case CandidateSkip::OtherMapGather:
                    ai->TellDebug(requester, "Skipping gathering destination on another map.", "debug travel");
                    ++lastRejects.otherMapGather;
                    continue;
                case CandidateSkip::OtherMapGrind:
                    ai->TellDebug(requester, "Skipping grind destination on another map (GrindMode 1).", "debug travel");
                    continue;
                case CandidateSkip::GatherPurposeSuppressed:
                    ++lastRejects.deathSuppressed;
                    continue;
                case CandidateSkip::None:
                    break;
            }

            // Re-deaths at camps (DestinationDeaths.CampBlock, CampBlockPolicy.h): a point near a recent own camp death is skipped,
            // a quest destination there only ranks lower (fallback below). A forced target (guild order) is never judged; with the
            // switch off or another cohort the bot's ring is empty and nothing is blocked.
            bool const campBlocked = !target->IsForced() && persistentTarget && persistentTarget->IsCampBlocked(position);
            camp_block::Verdict const campVerdict = campBlocked ?
                camp_block::Judge(true, dynamic_cast<QuestTravelDestination const*>(destination) != nullptr) : camp_block::Verdict::Free;
            if (campVerdict == camp_block::Verdict::Skip)
            {
                ai->TellDebug(requester, "Skipping destination near a recent camp death.", "debug travel");
                ++campSkipped;
                continue;
            }

            // twow-repo#601 GrindMode 2 (review): no forced kill trip to a creature that no active quest needs.
            if (forcedNoGrind && !ai::quest_grind_mode::ForcedKillTripAllowed(true, IsKillTripDestination(destination),
                    std::binary_search(forcedNeeded.begin(), forcedNeeded.end(), destination->GetEntry())))
            {
                if (!forcedKillSkipped++)
                    forcedKillEntry = destination->GetEntry();
                continue;
            }

            if (!target->IsForced() && isActive.find(destination) != isActive.end() && !isActive[destination])
            {
                ++lastRejects.knownInactive;
                continue;
            }

            if (!target->IsForced() && hasActiveLocalQuestHub)
            {
                if (travelInfoReuse)
                {
                    // A10: cheap checks first, then a known answer of this choice, IsActive only on a miss.
                    bool hubActive = IsLocalQuestHubCandidate(bot, destination, position);
                    if (hubActive)
                    {
                        auto const known = isActive.find(destination);
                        hubActive = known != isActive.end() ? known->second : destination->IsActive(bot, choiceInfo());
                        if (hubActive) // only a true answer is kept; a false one stays a hub rejection as before
                            isActive[destination] = true;
                    }
                    if (!hubActive)
                    {
                        ++lastRejects.hubFilter;
                        continue;
                    }
                }
                else
                {
                    PlayerTravelInfo travelInfo(bot);
                    if (!IsLocalActiveQuestHubDestination(bot, destination, position, travelInfo))
                    {
                        ++lastRejects.hubFilter;
                        continue;
                    }
                }
            }

            if (distanceCheck) //Check if we have moved significantly after getting the destinations.
            {
                WorldPosition center(requester ? requester : bot);
                if (position->distance(center) > distance * 2 && position->distance(center) > 100)
                {
                    ai->TellDebug(requester, "We had some destinations but we moved too far since. Trying to get a new list.", "debug travel");
                    ++lastRejects.movedAway;
                    return false;
                }

                distanceCheck = false;
            }

            // twow-repo#601 S2 R3 (review C-F2): the gap grind choice takes the reuse path (its window snapshot) with the switch off too.
            if (target->IsForced() || (travelInfoReuse ? reuseActive(destination) : (gapGrindChoice ? reuseActive(destination) :
                (isActive[destination] = destination->IsActive(bot, PlayerTravelInfo(bot))))))
            {
                // twow-repo#601 S3b: the deferrals after IsActive in one predicate (PostActiveDeferralOf, same order,
                // counters and traces as before) - the local quest hub pre-loop asks the same one with HubPreloopSameFilters.
                danger_map::DangerMap::Hit hit;
                switch (PostActiveDeferralOf(ai, bot, target, destination, position, hit))
                {
                    case PostActiveDeferral::BattlegroundQuest:
                        ++lastRejects.battlegroundQuest;
                        continue;
                    case PostActiveDeferral::EnemyZone:
                        ai->TellDebug(requester, "Skipping " + destination->GetTitle() + " - enemy home zone", "debug travel");
                        ++lastRejects.enemyZone;
                        continue;
                    case PostActiveDeferral::CrossMap:
                        ++deferredCrossMap;
                        ++lastRejects.crossMap;
                        continue;
                    case PostActiveDeferral::ZoneLevel:
                        ++deferredZoneLevel;
                        ++lastRejects.zoneLevel;
                        continue;
                    case PostActiveDeferral::DangerMap:
                        ++deferredDeathCluster;
                        deathClusterCells += hit.cells;
                        deathClusterWorstKillerLevel = std::max(deathClusterWorstKillerLevel, hit.worstKillerLevel);
                        ++lastRejects.dangerMap;
                        continue;
                    case PostActiveDeferral::None:
                        break;
                }

                // twow-repo#601 S1: not while the local quest hub filter is on with FailureClasses (the skip past the one
                // eligible local candidate left only hub-filtered partitions: 21 of 165 L10+ hub rejections).
                if (ai::quest_failure_class::RangeSkipAllowed(failureClasses, hasActiveLocalQuestHub) &&
                    partition != std::prev(partitionedList.end())->first && !urand(0, 10)) //10% chance to skip to a longer partition.
                {
                    ai->TellDebug(requester, "Skipping range " + PrintPartion(partition), "debug travel");
                    ++lastRejects.rangeSkip;
                    break;
                }

#ifdef MANGOSBOT_TWO
                if (GuidPosition* guidP = static_cast<GuidPosition*>(position))
                {
                    if (!bot->InSamePhase(guidP->GetPhaseMask()))
                    {
                        ai->TellDebug(requester, "Not same phase: " + destination->GetTitle() + " " + std::to_string(round(destination->DistanceTo(bot))) + "y", "debug travel");
                        continue;
                    }
                }
#endif

                // Re-deaths at camps: the first quest destination near a recent camp death that passed every check above is kept
                // and taken only when this choice selects nothing else (never a veto).
                if (campVerdict == camp_block::Verdict::Fallback)
                {
                    ++campRankedLower;
                    if (!campFallbackDestination)
                    {
                        campFallbackDestination = destination;
                        campFallbackPosition = position;
                    }
                    continue;
                }

                target->SetTarget(destination, position);
                hasTarget = true;
                break;
            }
            else
            {
                ai->TellDebug(requester, "Not active: " + destination->GetTitle() + " " + std::to_string((uint32)round(destination->DistanceTo(bot))) + "y", "debug travel");
                ++lastRejects.notActive;
                // twow-repo#594 / P3-0: the reason, as a split of notActive (counted above as before).
                // Judged with the choice's PlayerTravelInfo (choiceInfo: the one IsActive saw with A10 reuse, built once per
                // choice without it), not a new one per candidate (OB-10 review).
                if (sPlayerbotAIConfig.directTargetsTraceInactive)
                    ++lastRejects.notActiveReason[InactiveReasonOf(ai, destination, choiceInfo())];
            }

        }

        if (hasTarget || chooseBudgetExceeded)
            break;
    }

    // Re-deaths at camps: nothing else selected - the kept quest destination after all. Not on a budget-aborted slice (the next
    // slice resumes the search). One line per choice the rule touched.
    bool campFallbackTaken = false;
    if (!hasTarget && !chooseBudgetExceeded && campFallbackDestination)
    {
        target->SetTarget(campFallbackDestination, campFallbackPosition);
        hasTarget = campFallbackTaken = true;
    }
    if (campSkipped || campRankedLower)
        sLog.outBasic("[QuestFirstRoute] state=deferred bot=%u level=%u reason=camp_block skipped=%u ranked_lower=%u fallback_taken=%u selected_other=%u",
            bot->GetGUIDLow(), bot->GetLevel(), campSkipped, campRankedLower, campFallbackTaken ? 1u : 0u,
            hasTarget && !campFallbackTaken ? 1u : 0u);

    if(hasTarget)
        ai->TellDebug(requester, "Point at " + std::to_string(uint32(target->Distance(bot))) + "y selected.", "debug travel");

    if ((deferredCrossMap || deferredZoneLevel) && sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions)
        sLog.outBasic("[QuestFirstRoute] state=deferred bot=%u level=%u reason=route_danger cross_map=%u target_zone_level=%u selected_other=%u",
            bot->GetGUIDLow(), bot->GetLevel(), deferredCrossMap, deferredZoneLevel, hasTarget ? 1u : 0u);

    // Always visible (BASIC): acceptance signal for the shared danger map.
    if (deferredDeathCluster)
        sLog.outBasic("[QuestFirstRoute] state=deferred bot=%u level=%u reason=route_danger detail=death_cluster deferred=%u cells=%u worst_killer_level=%u selected_other=%u",
            bot->GetGUIDLow(), bot->GetLevel(), deferredDeathCluster, deathClusterCells, deathClusterWorstKillerLevel, hasTarget ? 1u : 0u);

    // twow-repo#601 GrindMode 2 (review): the forced order left only kill trips - no target, the bot stands idle; once per
    // choice (the caller then marks the purpose "no active travel destinations", so the order does not re-request).
    // Not on a budget-aborted slice: the caller returns without marking and the next slice may still find a target.
    if (!hasTarget && forcedKillSkipped && !chooseBudgetExceeded)
        ai->NoteQuestIdle(uint8(ai::quest_grind_mode::Reason::NoTarget),
            ai::quest_grind_mode::TargetText("forced_kill_trip", forcedKillEntry > 0 ? uint32(forcedKillEntry) : 0u));

    ai->AddTravelChooseChecked(checked);
    if (!chooseBudgetExceeded)
        chooseResume.Clear();
    else
    {
        uint32 const now = uint32(time(nullptr));
        if (now - lastChooseBudgetLog >= ai::travel_choose::LogSeconds)
        {
            lastChooseBudgetLog = now;
            sLog.outBasic("[TravelChoose] state=budget_exceeded bot=%u purpose=%s checked=%u resume_at=%u ms=%u map=%u zone=%u",
                bot->GetGUIDLow(), choosePurpose.c_str(), checked, chooseResume.skip,
                WorldTimer::getMSTimeDiffToNow(chooseStart), bot->GetMapId(), bot->GetZoneId());
        }
    }

    return hasTarget;
}

std::vector<std::string> split(const std::string& s, char delim);
char* strstri(const char* haystack, const char* needle);

//Find a destination based on (part of) it's name. Includes zones, ncps and mobs. Picks the closest one that matches.
DestinationList ChooseTravelTargetAction::FindDestination(PlayerTravelInfo info, std::string name, bool zones, bool npcs, bool quests, bool mobs, bool bosses, bool gather)
{
    DestinationList dests;

    //Quests
    if (quests)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Zones
    if (zones)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Explore, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Npcs
    if (npcs)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GenericRpg, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Mobs
    if (mobs)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Grind, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Bosses
    if (bosses)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Boss, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Gather
    if (gather)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherSkinning, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }

        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherMining, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }

        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherHerbalism, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    if (dests.empty())
        return {};

    return dests;
};

bool ChooseGroupTravelTargetAction::Execute(Event& event)
{
    std::vector<ObjectGuid> groupPlayers;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        if (ref->getSource() != bot)
        {
            groupPlayers.push_back(ref->getSource()->GetObjectGuid());
        }
    }

    std::shuffle(groupPlayers.begin(), groupPlayers.end(), *GetRandomGenerator());

    PlayerTravelInfo info(bot);

    std::vector<TravelTarget*> groupTargets;

    // twow-repo#601 S2 / S4 (GrindMode 1, review C-F3 / B-F2): no group copy of a grind trip for the gap filler's population - a
    // member's grind destination stays a grind for this bot unless it needs the creature for an active quest itself (one
    // sorted per-bot list, no value per creature); its own R1 request opens the window (R2 end, R3, own map). GrindMode 0 / 2:
    // IsQuestGapGrind() is false, nothing read.
    bool const gapGrind = ai->IsQuestGapGrind();
    std::vector<int32> gapNeeded;
    if (gapGrind)
        gapNeeded = AI_VALUE(std::vector<int32>, "needed quest entries");

    PartitionedTravelList travelList;

    std::unordered_map<TravelDestination*, std::vector<std::string>> conditions;
    std::unordered_map<TravelDestination*, Player*> playerDesitnations;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    //Find targets of the group.
    for (auto& member : groupPlayers)
    {
        Player* player = sObjectMgr.GetPlayer(member);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!GetBotAI(player))
            continue;

        if (!GetBotAI(player)->GetAiObjectContext())
            continue;

        TravelTarget* groupTarget = PAI_VALUE(TravelTarget*, "travel target");

        if (groupTarget->IsGroupCopy())
            continue;

        if (!groupTarget->IsActive())
            continue;

        if (groupTarget->IsForced())
            continue;

        if (gapGrind && !ai::quest_grind_mode::GapGroupCopyAllowed(true,
                dynamic_cast<GrindTravelDestination*>(groupTarget->GetDestination()) != nullptr,
                std::binary_search(gapNeeded.begin(), gapNeeded.end(), groupTarget->GetDestination()->GetEntry())))
            continue;

        // twow-repo#601 S5a (BandLadder): a band hub trip is no group candidate (no copy, no member COOLDOWN on it).
        if (groupTarget->GetDestination()->IsBandHub())
            continue;

        // twow-repo#601 S2 R3 (review C-F2): a member's gap grind trip (gapWindowEnd, GrindMode 1 only) is judged with its window.
        if (!groupTarget->GetDestination()->IsActive(player, PlayerTravelInfo(player, groupTarget->GetGapWindowEnd() != 0)) || !groupTarget->IsConditionsActive())
        {
            GetBotAI(player)->TellDebug(requester,"Target is cooling down because a group member found it to be inactive.", "debug travel");
            groupTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
            continue;
        }

        groupTargets.push_back(groupTarget);        
        playerDesitnations[groupTarget->GetDestination()] = player;
        conditions[groupTarget->GetDestination()] = groupTarget->GetConditions();
    }

    std::sort(groupTargets.begin(), groupTargets.end(), [](TravelTarget* i, TravelTarget* j) {return i->GetRelevance() > j->GetRelevance(); });

    ai->TellDebug(requester, std::to_string(groupTargets.size()) + " group targets found.", "debug travel");

    for (auto& groupTarget : groupTargets)
    {
        travelList[0].push_back(TravelPoint(groupTarget->GetDestination(), groupTarget->GetPosition(), groupTarget->GetPosition()->distance(bot)));

        ai->TellDebug(requester, playerDesitnations[groupTarget->GetDestination()]->GetName() + std::string(": ") + groupTarget->GetDestination()->GetShortName() + std::string(" (") + std::to_string(groupTarget->GetRelevance()) + std::string(")"), "debug travel");
    }

    if (travelList[0].empty())
        return false;

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    TravelTarget newTarget = TravelTarget(ai);

    if (!SetBestTarget(requester, &newTarget, travelList))
        return false;
    
    newTarget.SetGroupCopy(playerDesitnations[newTarget.GetDestination()]);

    setNewTarget(requester, &newTarget, oldTarget);

    oldTarget->SetConditions(conditions[newTarget.GetDestination()]);

    return true;
}

bool ChooseGroupTravelTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!bot->GetGroup())
        return false;

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (urand(0, 100) < 50)
        return false;

    return true;
}

bool RefreshTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    TravelDestination* oldDestination = target->GetDestination();

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    if (target->IsMaxRetry(false))
    {
        ai->TellDebug(requester, "Old destination was tried too many times.", "debug travel");
        return false;
    }

    if (!oldDestination) //Does this target have a destination?
        return false;

    // G4 (train 6.1, 2026-09-27): a route or destination put on the death
    // cooldown (#138, #307) or skipped by the zone escape must not come back
    // through the refresh. Isolanna died 16x on one turn-in route in an hour:
    // every death_suppressed was undone a minute later by this action.
    if (target->IsTurnInRouteSuppressed(oldDestination, target->GetPosition()) || target->IsDestinationDeathSuppressed(oldDestination) ||
        target->IsQuestLoopSuppressed(oldDestination))   // twow-repo#581 (QL-1)
    {
        ai->TellDebug(requester, "Old destination is on the death cooldown.", "debug travel");
        return false;
    }

    // twow-repo#485: nor may the taker of a parked turn-in (the refresh bypasses CopyTarget).
    QuestRelationTravelDestination const* oldTaker = dynamic_cast<QuestRelationTravelDestination const*>(oldDestination);
    if (oldTaker && oldTaker->GetRelation() && target->IsTurnInParked(oldTaker->GetQuestId()))
    {
        ai->TellDebug(requester, "Old destination is a parked turn-in.", "debug travel");
        return false;
    }

    if (!target->IsDestinationActive()) //Is the destination still valid?
    {
        ai->TellDebug(requester, "Old destination was no longer valid.", "debug travel");
        return false;
    }

    PlayerTravelInfo info(bot);
    
    WorldPosition* newPosition;

    for (uint8 i = 0; i < 5; i++)
    {
        std::list<uint8> chancesToGoFar = { 10,20,90 }; //Closest map, grid, cell.
        newPosition = oldDestination->GetNextPoint(*target->GetPosition(), chancesToGoFar);
        if (newPosition && sTravelMgr.IsLocationLevelValid(*newPosition, info, (uint32)oldDestination->GetPurpose()))
            break;        
    }

    if (!newPosition)
    {
        ai->TellDebug(requester, "No new locations found for old destination.", "debug travel");
        return false;
    }

    SET_AI_VALUE2(bool, "manual bool", "is travel refresh", true);
    bool conditionsStillActive = AI_VALUE(TravelTarget*, "travel target")->IsConditionsActive(true);
    RESET_AI_VALUE2(bool, "manual bool", "is travel refresh");

    if (!conditionsStillActive)
        return false;

    target->SetTarget(oldDestination, newPosition);

    target->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
    target->IncRetry(false);

    RESET_AI_VALUE(bool, "travel target active");    
    context->ClearValues("no active travel destinations");
    SET_AI_VALUE2(std::string, "manual string", "future travel detail", std::string());

    ai->TellDebug(requester, "Refreshed travel target", "debug travel");
    ReportTravelTarget(bot, requester, target, target);

    return false;
}

bool RefreshTravelTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    // twow-repo#601 S2 / S4 R2 (GrindMode 1, review B-F5): no refresh of a gap grind trip, in or after its window - no renewal,
    // and no READY again on an ended window (that would count expired= twice). gapWindowEnd: GrindMode 1 only. Before the
    // skip chance (no urand drawn).
    if (AI_VALUE(TravelTarget*, "travel target")->GetGapWindowEnd())
        return false;

    // twow-repo#601 S5a (BandLadder): no refresh of a band hub trip, running or ended (no renewal, no hub trip without an end).
    // Before the skip chance (no urand drawn). Only band hubs answer true (QuestBand.h).
    if (AI_VALUE(TravelTarget*, "travel target")->GetDestination()->IsBandHub())
        return false;

    if (!WorldPosition(bot).isOverworld())
        return false;

    if (urand(1, 100) <= 10)
        return false;

    if (!AI_VALUE(TravelTarget*, "travel target")->GetDestination()->IsActive(bot, PlayerTravelInfo(bot)))
        return false;

    return true;
}

bool ResetTargetAction::Execute(Event& event)
{
    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    context->ClearValues("no active travel destinations");

    TravelTarget newTarget = TravelTarget(ai);
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    setNewTarget(requester, &newTarget, oldTarget);

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
    oldTarget->SetExpireIn(60000); //1 minute;

    ai->TellDebug(requester, "Cleared travel target fetches", "debug travel");

    return true;
}

bool ResetTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    return true;
}

bool RequestTravelTargetAction::Execute(Event& event)
{
    // #416: no new destination job while the parking lot is full.
    if (!FutureDestinations::MayStart())
        return false;

    TravelDestinationPurpose actionPurpose = TravelDestinationPurpose(stoi(getQualifier()));

    // A persistent roster bot with a real player master must not trade Follow
    // for an autonomous RPG, grind, trainer, vendor, or distant gathering trip.
    // Skinning remains a local loot interaction rather than a travel purpose;
    // herb/mining use the existing travel pipeline but are constrained below.
    if (IsAutonomousRosterTravelBlocked(ai, actionPurpose) && !event.getOwner())
        return false;

    // twow-repo#601 S2 / S4 R1 / R2 (GrindMode 1, recheck of review B-F3, OB-10 18.10.2026): a grind request of the gap filler's
    // population - isUseful let it through only inside the R1 window, whichever trigger queued it (the money trigger, 6.77,
    // and the plain one, 6.35, share this action and Queue::Push keeps the event of the higher relevance) - starts the gap
    // trip with R1 as its condition (GapTripCondition below), never the money condition, so the trip ends with the window.
    // GrindMode 0 / 2: IsQuestGapGrind() is false, the event source as before.
    bool const gapGrindRequest = ai->IsQuestGapGrind() && actionPurpose == TravelDestinationPurpose::Grind;

    WorldPosition center = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);
    bool const enforceGatherLeash = UsesQuestFirstProgression(bot) && ai->HasRealPlayerMaster() &&
        IsGatherLeashPurpose(actionPurpose);
    WorldPosition const masterPosition(enforceGatherLeash ? ai->GetMaster() : nullptr);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for " + TravelDestinationPurposeName.at(actionPurpose), "debug travel");

    // twow-repo#601 S2 R3 (review C-F2): the R3 window snapshot only for the grind fetch (and only for the GrindMode 1 scope).
    *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async,
        [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot, actionPurpose == TravelDestinationPurpose::Grind), center, purpose = actionPurpose,
            enforceGatherLeash, masterPosition]()
        {
            PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)purpose);
            if (!enforceGatherLeash)
                return list;

            // Preserve the manager's normal destination and ownership semantics;
            // only remove resource targets that would pull this bot away from the
            // real master. The master position is an immutable value captured
            // before the worker starts.
            for (auto& [partition, points] : list)
            {
                points.erase(std::remove_if(points.begin(), points.end(), [&masterPosition](TravelPoint const& point)
                {
                    WorldPosition const* position = std::get<1>(point);
                    return !position || position->getMapId() != masterPosition.getMapId() ||
                        position->distance(masterPosition) > 25.0f;
                }), points.end());
            }
            return list;
        });

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", getQualifier());
    SET_AI_VALUE2(std::string, "manual string", "future travel condition",
        ai::quest_grind_mode::GapTripCondition(gapGrindRequest, event.getSource(), uint32(TravelDestinationPurpose::Grind)));
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestTravelTargetAction::isUseful() {
    if (bot->InBattleGround())
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (AI_VALUE(bool, "travel target active"))
        return false;

    if (AI_VALUE2(bool, "no active travel destinations", (getQualifier().empty() ? "quest" : getQualifier())))
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    // twow-repo#601 GrindMode 2 (owner 17.10.2026): no pure grind fetch for a quest-first roster bot on its own - neither
    // the money grind ("val::should get money", 6.77, also with GrindFallbackFailures 0) nor the plain grind request
    // (6.35; its need-travel-purpose value is already false). Before isAllowed, so the skip chance draws as before.
    if (ai->IsQuestNoGrind() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)))
    {
        ai->NoteQuestIdle(uint8(ai::quest_grind_mode::RequestReason(AI_VALUE(bool, "should get money"))),
            ai::quest_grind_mode::TargetText("request_travel_grind", 0));
        return false;
    }
    // twow-repo#601 decision order (QuestFirstOrder, owner 20.10.2026; CLI-Deep review): the same for its population - no grind
    // fallback (8.32) and no money grind as a substitute for quest work. Counted per refused request (grind_refused=).
    if (ai->IsQuestFirstOrder() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)))
    {
        PlayerbotAI::NoteQuestOrder(uint8(ai::quest_first_order::Event::GrindRefused));
        ai->NoteQuestOrderEpisode(uint8(ai::quest_first_order::Event::GrindRefused));
        return false;
    }

    // twow-repo#601 S2 / S4 (GrindMode 1, OB-10 rev. 2, OB-10 18.10.2026): no money grind for the gap filler's population outside
    // its R1 window - a grind fetch only inside it (the need-travel-purpose value), whichever trigger queued the request (the
    // money trigger, 6.77, or the plain one, 6.35: one basket, Queue::Push); Execute gives the trip R1 as its condition, so it
    // never outlives the window. Before the skip chance (no urand drawn). GrindMode 0 / 2: IsQuestGapGrind() is false.
    if (ai->IsQuestGapGrind() && getQualifier() == std::to_string(uint32(TravelDestinationPurpose::Grind)) &&
        !AI_VALUE2(bool, "need travel purpose", getQualifier()))
        return false;

    if (!isAllowed())
    {
        ai->TellDebug(ai->GetMaster(), "Skipped " + GetTravelPurposeName(qualifier) + " because of skip chance", "debug travel");
        return false;
    }

    return true;
}

bool RequestTravelTargetAction::isAllowed() const
{
    TravelDestinationPurpose actionPurpose = TravelDestinationPurpose(stoi(getQualifier()));

    switch (actionPurpose)
    {
    case TravelDestinationPurpose::Repair:
    case TravelDestinationPurpose::Vendor:
    case TravelDestinationPurpose::AH:
        return urand(1, 100) < 90;
    case TravelDestinationPurpose::Mail:
        if (!AI_VALUE(bool, "should get money"))
            return urand(1, 100) < 30;
        else
            return true;
    case TravelDestinationPurpose::GatherSkinning:
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherHerbalism:
    case TravelDestinationPurpose::GatherFishing:
        if (bot->GetGroup())
            return urand(1, 100) < 50;
        else
            return urand(1, 100) < 90;
    case TravelDestinationPurpose::Boss:
        return urand(1, 100) < 50;
    case TravelDestinationPurpose::Explore:
        return urand(1, 100) < 10;
    case TravelDestinationPurpose::GenericRpg:
        return urand(1, 100) < 50;
    case TravelDestinationPurpose::Grind:
        return true;
    default:
        return true;
    }
}

bool RequestNamedTravelTargetAction::Execute(Event& event)
{
    // #416: no new destination job while the parking lot is full.
    if (!FutureDestinations::MayStart())
        return false;

    // Strategy-triggered named travel is autonomous. A command from the real
    // master carries its owner and remains an explicit instruction.
    if (UsesQuestFirstProgression(bot) && ai->HasRealPlayerMaster() && !event.getOwner())
        return false;

    std::string travelName = getQualifier();

    WorldPosition center = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for travel " + getQualifier(), "debug travel");

    if (travelName == "pvp")
    {
        std::string WorldPvpLocation;

        //Number between 0 and 100 synced for all bots that shifts 1 every 10 minutes.
        uint32 pvpLocationNumber = ai->GetFixedBotNumber(BotTypeNumber::WORLD_PVP_LOCATION, 100, 0.1f, true);

        if (pvpLocationNumber < 20) //First 200 minutes
            WorldPvpLocation = "Tarren Mill";
        else if (pvpLocationNumber >= 20 && pvpLocationNumber < 40) //Second 200 minutes
            WorldPvpLocation = "The Barrens";
        else if (pvpLocationNumber >= 40 && pvpLocationNumber < 60) //Third 200 minutes
            WorldPvpLocation = "Silithus";
        else if (pvpLocationNumber >= 60 && pvpLocationNumber < 80) //Fourth 200 minutes
            WorldPvpLocation = "Eastern Plaguelands";
        else                                                        //Last 200 minutes
            WorldPvpLocation = "Strangletorn Vale";

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [travelInfo = PlayerTravelInfo(bot), center, WorldPvpLocation]()
            {
                PartitionedTravelList list;
                for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, WorldPvpLocation, true, false, false, false, false, false))
                {
                    std::list<uint8> chancesToGoFar = { 10,50,90 }; //Closest map, grid, cell.
                    WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);

                    if (!point)
                        continue;

                    list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                }

                return list;
            }
        );
    }
    else if (travelName == "guild meeting")
    {
        // Parse guild MOTD for the meeting time.
        // Meeting: <location> <start time> <end time>
        std::string meetingLocation;
        if (bot->GetGuildId())
        {
            Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
            if (guild)
            {
                std::string motd = guild->GetMOTD();
                auto pos = motd.find("Meeting:");
                if (pos != std::string::npos)
                {
                    std::string body = motd.substr(pos + 8);
                    body.erase(body.begin(), std::find_if(body.begin(), body.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                    std::vector<std::string> tokens;
                    { std::istringstream iss(body); std::string t; while (iss >> t) tokens.push_back(t); }
                    if (tokens.size() >= 3)
                    {
                        tokens.pop_back(); // end time
                        tokens.pop_back(); // start time
                        std::ostringstream loc;
                        for (size_t i = 0; i < tokens.size(); ++i) { if (i) loc << " "; loc << tokens[i]; }
                        meetingLocation = loc.str();
                    }
                }
            }
        }

        if (meetingLocation.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No meeting location found in guild MOTD", "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [travelInfo = PlayerTravelInfo(bot), center, meetingLocation]()
            {
                PartitionedTravelList list;
                for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, meetingLocation, true, false, false, false, false, false))
                {
                    std::list<uint8> chancesToGoFar = { 10,50,90 };
                    WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);

                    if (!point)
                        continue;

                    list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                }

                return list;
            }
        );
    }
    else if (travelName == "guild order")
    {
        GuildOrder order = AI_VALUE(GuildOrder, "guild order");

        if (!order.IsTravelOrder())
        {
            ai->TellDebug(ai->GetMaster(), "No valid guild travel order found", "debug travel");
            return false;
        }

        std::string orderTarget = order.target;

        ai->TellDebug(ai->GetMaster(), "Guild order: " + order.GetTypeName() + " " + orderTarget, "debug travel");

        if (order.type == GuildOrderType::QuestReward)
        {
            uint32 questId = order.questId;
            if (!questId)
            {
                ai->TellDebug(ai->GetMaster(), "QuestReward order has no questId", "debug travel");
                return false;
            }

            QuestStatus questStatus = bot->GetQuestStatus(questId);
            bool questComplete = false;
            bool questInProgress = false;

            if (questStatus == QUEST_STATUS_COMPLETE)
                questComplete = true;
            else if (questStatus == QUEST_STATUS_INCOMPLETE)
                questInProgress = true;

            if (!questComplete && questStatus == QUEST_STATUS_INCOMPLETE)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest && bot->CanRewardQuest(quest, false))
                    questComplete = true;
            }

            std::vector<int32> objectiveEntries;
            std::vector<int32> questGiverEntries;
            std::vector<int32> questTakerEntries;

            if (questInProgress && !questComplete)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest)
                {
                    for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; objective++)
                    {
                        std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };
                        if (!AI_VALUE2(bool, "need quest objective", Qualified::MultiQualify(qualifier, ",")))
                            continue;

                        if (quest->ReqCreatureOrGOId[objective])
                            objectiveEntries.push_back(quest->ReqCreatureOrGOId[objective]);

                        if (quest->ReqItemId[objective])
                        {
                            std::list<int32> dropList = GAI_VALUE2(std::list<int32>, "item drop list", quest->ReqItemId[objective]);
                            for (int32 entry : dropList)
                                objectiveEntries.push_back(entry);

                            std::list<int32> vendorList = GAI_VALUE2(std::list<int32>, "item vendor list", quest->ReqItemId[objective]);
                            for (int32 entry : vendorList)
                                objectiveEntries.push_back(entry);
                        }
                    }
                }
            }

            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async,
                [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, questId,
                questComplete, questInProgress, objectiveEntries]()
                {
                    PartitionedTravelList list;

                    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                    if (!quest)
                        return list;

                    if (questComplete)
                    {
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            (uint32)TravelDestinationPurpose::QuestTaker, {}, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                        {
                            for (auto& point : points)
                            {
                                QuestTravelDestination* questDest = dynamic_cast<QuestTravelDestination*>(std::get<TravelDestination*>(point));
                                if (questDest && questDest->GetQuestId() == questId)
                                    list[partition].push_back(point);
                            }
                        }
                    }
                    else if (questInProgress && !objectiveEntries.empty())
                    {
                        uint32 allObjectiveFlags = (uint32)TravelDestinationPurpose::QuestAllObjective;
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            allObjectiveFlags, objectiveEntries, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                            list[partition].insert(list[partition].end(), points.begin(), points.end());

                        if (list.empty())
                        {
                            subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                                (uint32)TravelDestinationPurpose::Grind, objectiveEntries, false, 1000000.0f);
                            for (auto& [partition, points] : subList)
                                list[partition].insert(list[partition].end(), points.begin(), points.end());
                        }
                    }
                    else
                    {
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                        {
                            for (auto& point : points)
                            {
                                QuestTravelDestination* questDest = dynamic_cast<QuestTravelDestination*>(std::get<TravelDestination*>(point));
                                if (questDest && questDest->GetQuestId() == questId)
                                    list[partition].push_back(point);
                            }
                        }
                    }

                    return list;
                }
            );

            SET_AI_VALUE2(std::string, "manual string", "future travel detail", orderTarget);
        }
        else if (order.type == GuildOrderType::Farm || order.type == GuildOrderType::Kill)
        {
            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [travelInfo = PlayerTravelInfo(bot), center, orderTarget, partitions = travelPartitions]()
                {
                    PartitionedTravelList list;

                    uint32 foundItemId = GuildOrderValue::FindItemByName(orderTarget);

                    if (foundItemId)
                    {
                        std::list<int32> dropEntries = GAI_VALUE2(std::list<int32>, "item drop list", foundItemId);

                        if (!dropEntries.empty())
                        {
                            std::vector<int32> gatherEntries, mobEntries;
                            for (int32 entry : dropEntries)
                            {
                                if (entry < 0)
                                    gatherEntries.push_back(entry);
                                else
                                    mobEntries.push_back(entry);
                            }

                            // Check which gathering skills the bot actually has.
                            bool hasHerbalism = travelInfo.GetCurrentSkill(SKILL_HERBALISM) > 0;
                            bool hasMining = travelInfo.GetCurrentSkill(SKILL_MINING) > 0;
                            bool hasSkinning = travelInfo.GetCurrentSkill(SKILL_SKINNING) > 0;
                            bool hasAnyGathering = hasHerbalism || hasMining || hasSkinning;

                            // Bot has a gathering skill: prioritize gather nodes.
                            if (!gatherEntries.empty() && hasAnyGathering)
                            {
                                // Only query gather purposes the bot can actually use.
                                if (hasHerbalism)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherHerbalism, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (hasMining)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherMining, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (hasSkinning)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherSkinning, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                            }

                            // If entry-based gather lookup failed, try unfiltered gather by purpose
                            // (the travel manager may index nodes by their own entry, not drop-source entry).
                            if (list.empty() && hasAnyGathering && !gatherEntries.empty())
                            {
                                if (hasHerbalism)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherHerbalism);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (list.empty() && hasMining)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherMining);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (list.empty() && hasSkinning)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherSkinning);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                            }

                            // Fall back to mob drops only if no gather nodes were found or bot has no gathering skill.
                            if (list.empty() && !mobEntries.empty() && !hasAnyGathering)
                            {
                                uint32 mobPurpose = (uint32)TravelDestinationPurpose::Grind;

                                list = sTravelMgr.GetPartitions(center, partitions, travelInfo, mobPurpose, mobEntries, false);
                            }
                        }
                    }

                    // Fall back by name: if bot has gathering skills, try gather-only first.
                    if (list.empty())
                    {
                        bool hasHerbalism = travelInfo.GetCurrentSkill(SKILL_HERBALISM) > 0;
                        bool hasMining = travelInfo.GetCurrentSkill(SKILL_MINING) > 0;
                        bool hasSkinning = travelInfo.GetCurrentSkill(SKILL_SKINNING) > 0;
                        bool hasAnyGathering = hasHerbalism || hasMining || hasSkinning;

                        // Try gather nodes by name first if bot can gather.
                        if (hasAnyGathering)
                        {
                            for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, false, false, false, false, false, true))
                            {
                                std::list<uint8> chancesToGoFar = { 10,50,90 };
                                WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                                if (!point) continue;
                                list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                            }
                        }

                        // If still empty, fall back to mobs, bosses and gather nodes.
                        if (list.empty())
                        {
                            bool includeMobs = !hasAnyGathering;
                            for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, false, includeMobs, false, includeMobs, includeMobs, true))
                            {
                                std::list<uint8> chancesToGoFar = { 10,50,90 };
                                WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                                if (!point) continue;
                                list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                            }
                        }
                    }

                    return list;
                }
            );
        }
        else if (order.type == GuildOrderType::Explore)
        {
            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [travelInfo = PlayerTravelInfo(bot), center, orderTarget]()
                {
                    PartitionedTravelList list;
                    for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, true, false, false, false, false, false))
                    {
                        std::list<uint8> chancesToGoFar = { 10,50,90 };
                        WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                        if (!point) continue;
                        list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                    }

                    return list;
                }
            );
        }
        else if (order.type == GuildOrderType::AuctionHouse)
        {
            *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
                {
                    PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GenericRpg);

                    for (auto& [partition, travelPoints] : list)
                    {
                        travelPoints.erase(std::remove_if(travelPoints.begin(), travelPoints.end(), [](TravelPoint point)
                            {
                                EntryTravelDestination* dest = (EntryTravelDestination*)std::get<TravelDestination*>(point);
                                if (!dest->GetCreatureInfo())
                                    return true;

                                if (dest->GetCreatureInfo()->NpcFlags & UNIT_NPC_FLAG_AUCTIONEER)
                                    return false;

                                return true;
                            }), travelPoints.end());
                    }
                    return list;
                });
        }
        else
        {
            return false;
        }

        SET_AI_VALUE2(std::string, "manual string", "future travel detail", orderTarget);
    }
    // twow-repo#333 Berufe P1-a / P1-b (ProfessionTravelPolicy.h): the trainers of a GREEN phase-one rank, or the vendors
    // of the missing pick / knife. Before the generic "trainer" branch. The request time starts the 30-minute hold and
    // the hourly limit, also when no destination comes back (no request loop). One line per request.
    else if (travelName == "trainer profession" || travelName == "tool vendor")
    {
        bool const trainerTrip = travelName == "trainer profession";
        uint32 const tool = trainerTrip ? 0 : MissingGatherTool(ai);
        std::vector<int32> entries;
        if (trainerTrip)
            entries = PhaseOneProfessionTrainers(ai);
        else if (tool)
            for (int32 const entry : GAI_VALUE2(std::list<int32>, "item vendor list", tool))
                entries.push_back(entry);

        std::string const key = trainerTrip ? ai::profession_travel::kTrainerTripValue : ai::profession_travel::kToolTripValue;
        uint32 const now = uint32(time(nullptr));
        uint32 const backoffUntil = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until")));
        uint32 const holdUntil = ai::profession_travel::HoldUntil(now, backoffUntil);
        // A trip of this kind still open (its target expired without the hold being evaluated again) ends here.
        if (AI_VALUE2(int, "manual int", key + ai::profession_travel::kOpenSuffix))
            sLog.outBasic("[ProfTravel] state=end kind=%s bot=%u level=%u cohort=%c reason=lost trip_s=%u hold_over_s=%u",
                trainerTrip ? "trainer" : "tool", bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()),
                now - std::min(now, uint32(std::max(0, AI_VALUE2(int, "manual int", key)))),
                ai::profession_travel::HoldOverSeconds(now, uint32(std::max(0, AI_VALUE2(int, "manual int", key + ai::profession_travel::kBackoffEndSuffix)))));
        SET_AI_VALUE2(int, "manual int", key, int(now));
        SET_AI_VALUE2(int, "manual int", key + ai::profession_travel::kHoldUntilSuffix, int(holdUntil));
        SET_AI_VALUE2(int, "manual int", key + ai::profession_travel::kBackoffEndSuffix, int(backoffUntil));
        SET_AI_VALUE2(int, "manual int", key + ai::profession_travel::kOpenSuffix, entries.empty() ? 0 : 1);
        sLog.outBasic("[ProfTravel] state=request kind=%s bot=%u level=%u cohort=%c tool=%u entries=%u hold_s=%u",
            trainerTrip ? "trainer" : "tool", bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()), tool,
            uint32(entries.size()), holdUntil - now);

        if (entries.empty())
            return false;

        TravelDestinationPurpose const purpose = trainerTrip ? TravelDestinationPurpose::Trainer : TravelDestinationPurpose::Vendor;
        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [entries, purpose, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)purpose, entries, false);
            });
    }
    else if (travelName.find("trainer") == 0)
    {
        TrainerType type = TRAINER_TYPE_CLASS;

        if (travelName == "trainer mount")
            type = TRAINER_TYPE_MOUNTS;
        if (travelName == "trainer trade")
            type = TRAINER_TYPE_TRADESKILLS;
        if (travelName == "trainer pet")
            type = TRAINER_TYPE_PETS;

        std::vector<int32> trainerEntries = AI_VALUE2(std::vector <int32>, "available trainers", type);

        if (trainerEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No trainer entries found for " + getQualifier(), "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [entries = trainerEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Trainer, entries, false);
            });
    }
    else if (travelName == "mount")
    {
        std::vector<int32> mountVendorEntries = AI_VALUE(std::vector <int32>, "available mount vendors");

        if (mountVendorEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No vendor entries found for " + getQualifier(), "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [entries = mountVendorEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Vendor, entries, false);
            });
    }
    else if (travelName == "reagent vendor")
    {
        std::set<int32> reagentVendorEntrySet;
        std::vector<uint32> missingReagents = NeedsProfessionReagentsValue::GetMissingReagents(ai);
        for (uint32 reagentId : missingReagents)
        {
            std::list<int32> vendorEntries = GAI_VALUE2(std::list<int32>, "item vendor list", reagentId);
            for (int32 entry : vendorEntries)
                reagentVendorEntrySet.insert(entry);
        }

        std::vector<int32> reagentVendorEntries(reagentVendorEntrySet.begin(), reagentVendorEntrySet.end());

        if (reagentVendorEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No reagent vendor entries found", "debug travel");
            return false;
        }

        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [entries = reagentVendorEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Vendor, entries, false);
            });
    }
    else
    {
        uint32 useFlags;

        if (travelName == "city")
            useFlags = NPCFlags::UNIT_NPC_FLAG_BANKER | NPCFlags::UNIT_NPC_FLAG_BATTLEMASTER | NPCFlags::UNIT_NPC_FLAG_AUCTIONEER;
        else if (travelName == "tabard")
            useFlags = NPCFlags::UNIT_NPC_FLAG_TABARDDESIGNER;
        else if (travelName == "petition")
            useFlags = NPCFlags::UNIT_NPC_FLAG_PETITIONER;


        *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async, [cityFlags = useFlags, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GenericRpg);

                for (auto& [partition, travelPoints] : list)
                {
                    travelPoints.erase(std::remove_if(travelPoints.begin(), travelPoints.end(), [cityFlags](TravelPoint point)
                        {
                            EntryTravelDestination* dest = (EntryTravelDestination*)std::get<TravelDestination*>(point);
                            if (!dest->GetCreatureInfo())
                                return true;

                            if (dest->GetCreatureInfo()->NpcFlags & cityFlags)
                                return false;

                            return true;
                        }), travelPoints.end());
                }
                return list;
            });
    }

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", getQualifier());
    SET_AI_VALUE2(std::string, "manual string", "future travel condition",
        travelName == "guild meeting" ? "should travel named::guild meeting" :
        travelName == "guild order" ? "should travel named::guild order" :
        // twow-repo#333 Berufe P1-a / P1-b: the trip holds while its reason lasts, not only while the start gap does.
        travelName == "trainer profession" ? "should travel named::trainer profession hold" :
        travelName == "tool vendor" ? "should travel named::tool vendor hold" :
        event.getSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestNamedTravelTargetAction::isAllowed() const
{
    std::string name = getQualifier();
    if (name == "city")
    {
        if (urand(1, 100) > 10)
            return false;
        return true;
    }
    else if (name == "pvp")
    {
        if (urand(0, 4))
            return false;
        return true;
    }
    else if (name == "guild meeting")
        return true;
    else if (name == "reagent vendor")
        return true;
    else if (name == "trainer profession" || name == "tool vendor")   // twow-repo#333 Berufe P1-a / P1-b
        return true;
    else if (name == "guild order")
        return true;
    else if (name == "mount")
    {
        if (urand(1, 100) > 100)
            return false;
        return true;
    }
    else if (name.find("trainer") == 0)
    {
        if (urand(1, 100) > 100)
            return false;
        return true;
    }
    else if (name == "tabard")
        return true;
    else if (name == "petition")
        return true;

    return false;
}

bool RequestQuestTravelTargetAction::Execute(Event& event)
{
    // #416: no new destination job while the parking lot is full.
    if (!FutureDestinations::MayStart())
        return false;

    // Hotfix 8.1: the last quest route request found nothing - back off.
    if (UsesQuestFirstProgression(bot) &&
        uint32(time(nullptr)) < uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until"))))
        return false;

    // The normal player catch-up command supplies an owner. Do not let the
    // autonomous quest strategy move a roster bot away from its real master.
    if (UsesQuestFirstProgression(bot) && ai->HasRealPlayerMaster() && !event.getOwner())
        return false;

    WorldPosition center = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);

    focusQuestTravelList focusList = AI_VALUE(focusQuestTravelList, "focus travel target");
    RetireOneSafeStaleQuest(ai, focusList);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for travel quest", "debug travel");

    // Both search radii below scale with level, which keeps a low level bot near
    // home - a reasonable aim, undone by where the floor sits. At level 1 the
    // pickup radius was 410 yards and the active-quest radius 1075; a starting
    // zone is several thousand across. The bot took a quest, drifted away from
    // the giver while grinding, and could no longer see it: no destination came
    // back, `request quest travel target` returned false, and the engine fell
    // through to `attack anything` for good. Measured on a live realm: ten bots
    // between level 1 and 7 held 23 completed quests between them and logged not
    // one travel event, while the level 10-60 population - radius 8500 upwards -
    // travelled normally. A floor large enough to cover the zone you are standing
    // in fixes that without giving a level 1 bot the run of the continent.
    // #421 B: the quest-giver radius widens while nothing was found (stage 1
    // neighbouring zones, stage 2 the region). A bot standing in a zone
    // clearly below its level starts at the neighbouring zones.
    uint32 searchStage = 0;
    if (UsesQuestFirstProgression(bot))
    {
        searchStage = uint32(std::max(0, AI_VALUE2(int, "manual int", "quest search stage")));
        // twow-repo#575 (PATH-01 E5): not in the own start zone up to level 12 (the high-elf spine runs to L11).
        if (!searchStage && ai::quest_search::ZoneBelowBot(WorldPosition(bot).getAreaLevelOrParent(), bot->GetLevel()) &&
            !ai::start_zone::ZoneLevelExempt(sPlayerbotAIConfig.customRaceStartZones, bot->getRace(), bot->GetLevel(),
                bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY()))
            searchStage = 1;
    }
    std::vector<std::tuple<uint32, int32, float>> destinationFetches = { {(uint32)TravelDestinationPurpose::QuestGiver, 0,
        ai::quest_search::GiverRadius(bot->GetLevel(), searchStage)} };

    // twow-repo#485: this bot's parked turn-ins (TravelTarget::NoteTurnInFailure).
    TravelTarget* parkTarget = AI_VALUE(TravelTarget*, "travel target");

    // twow-repo#601 S1 (AiPlayerbot.QuestFirstProgression.FailureClasses, OB-10 answer 3): the bot's open turn-ins are
    // checked before the fetch is built - a structural one is parked now (the loop below then skips its taker), only a
    // routable one may make this request turn-in-only. Not for a bot led by a real player (unchanged).
    bool const failureClasses = sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) &&
        UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster();
    std::set<uint32> validatedTurnIns;
    if (failureClasses)
        validatedTurnIns = PreValidateTurnIns(ai, bot, parkTarget);

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (player->GetMapId() != bot->GetMapId())
            continue;

        if (!GetBotAI(player))
            continue;

        QuestStatusMap& questMap = player->getQuestStatusMap();

        bool onlyClassQuest = bot == player && !urand(0, 10);

        //Find destinations related to the active quests.
        for (auto& [questId, questStatus] : questMap)
        {
            uint32 flag = 0;
            if (questStatus.m_rewarded)
                continue;

            Quest const* questTemplate = sObjectMgr.GetQuestTemplate(questId);

            if (!questTemplate)
                continue;

            if (player->CanRewardQuest(questTemplate, false))
            {
                // twow-repo#485: a turn-in this bot parked gets no taker fetch, and neither does
                // a group member's copy of that quest (its CopyTarget would refuse the taker on
                // every request). The targets of the other quests come back.
                if (parkTarget->IsTurnInParked(questId))
                    continue;

                flag = (uint32)TravelDestinationPurpose::QuestTaker;
            }
            else
            {
                for (uint32 objective = 0; objective < 4; objective++)
                {
                    TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << (objective + 1));

                    std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };

                    if (AI_VALUE2(bool, "group or", "following party,need quest objective::" + Qualified::MultiQualify(qualifier, ","))) //Noone needs the quest objective.
                        flag = flag | (uint32)purposeFlag;
                }
            }

            if (!flag)
                continue;

            // Quadratic in level, so a level 20 bot searches thirty times further
            // than a level 1 one. The quest giver a bot has to walk back to is by
            // definition inside its own zone, whatever its level.
            // A completed quest is not a new autonomous offer. Its taker must
            // be found through TravelMgr even when it is beyond the ordinary
            // local objective radius; TravelMgr still rejects inactive or
            // unreachable destinations and supplies normal transport routing.
            float const range = (flag & (uint32)TravelDestinationPurpose::QuestTaker) ? 1000000.0f :
                std::max(5000.f, 1000.f + (bot->GetLevel() * bot->GetLevel()) * 75.f);
            destinationFetches.push_back({ flag, questId, range });

            if (onlyClassQuest && destinationFetches.size() > 1) //Only do class quests if we have any.
            {
                Quest const* firstQuest = sObjectMgr.GetQuestTemplate(std::get<1>(destinationFetches[1]));

                if (firstQuest->GetRequiredClasses() && !questTemplate->GetRequiredClasses())
                    continue;

                if (!firstQuest->GetRequiredClasses() && questTemplate->GetRequiredClasses())
                    destinationFetches = { destinationFetches.front() };
            }
        }
    }

    // A destination is picked by distance alone: SetBestTarget walks the
    // partitions from near to far and takes the first active point in the
    // nearest one. An objective is close by nature - the bot is standing where
    // it grinds - while the giver it has to return to is back in town. So
    // objectives win nearly every time. Measured over 220 minutes with ~1000
    // bots: 392 objective journeys an hour against 119 turn-ins, while 740
    // quests an hour were accepted. Five taken for every one handed in, so the
    // log can only fill; 751 bots sat at the cap and 138 of them held twenty
    // finished quests they could no longer act on. That also costs them their
    // gear, quest rewards being the only source of it.
    //
    // So once a bot is carrying finished work, the turn-in stops competing on
    // distance and simply wins. Counted here rather than taken from
    // getQuestStatusMap().size(), which includes already rewarded entries and
    // reads above the cap - the probe below saw an average of 22 against a
    // limit of 20.
    //
    // twow-repo#485: with turn-in parking on, a finished quest counts only while the taker
    // fetch above offers it (the bot can hand it in and has not parked it); 0 counts every
    // completed one. The quests a turn-in-only request offers takers for are noted for
    // ChooseTravelTargetAction::Execute, which counts them as no_route if no taker is reached.
    bool const turnInParking = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures > 0;
    std::string handInQuests;
    {
        uint32 finished = 0, active = 0;
        for (auto& [questId, questStatus] : bot->getQuestStatusMap())
        {
            if (questStatus.m_rewarded)
                continue;

            active++;
            if (questStatus.m_status == QUEST_STATUS_COMPLETE && (!turnInParking || parkTarget->IsTurnInOpen(questId)))
                finished++;
        }

        if ((UsesQuestFirstProgression(bot) && finished > 0) || finished >= 5 || active + 2 >= MAX_QUEST_LOG_SIZE)
        {
            std::vector<std::tuple<uint32, int32, float>> handInOnly;
            // twow-repo#601 S1: with FailureClasses only the takers checked above (TakerOnlyOffer); none = no
            // turn-in-only request, the normal quest giver / objective fetch runs.
            for (auto& fetch : destinationFetches)
                if ((std::get<0>(fetch) & (uint32)TravelDestinationPurpose::QuestTaker) &&
                    ai::quest_failure_class::TakerOnlyOffer(failureClasses, validatedTurnIns.count(uint32(std::get<1>(fetch))) > 0))
                    handInOnly.push_back(fetch);

            // Only if there is somewhere to hand in. An empty list would fall
            // through to the QuestGiver fetch below and send a bot that cannot
            // accept anything off to collect more.
            if (!handInOnly.empty())
            {
                destinationFetches = handInOnly;

                // Own open turn-ins only, as counted above, each once: a group member's turn-in
                // may be offered too, also for a quest this bot handed in already (it stays
                // QUEST_STATUS_COMPLETE) or cannot hand in.
                std::set<uint32> ownQuests;
                if (turnInParking)
                    for (auto& fetch : handInOnly)
                        if (parkTarget->IsTurnInOpen(uint32(std::get<1>(fetch))))
                            ownQuests.insert(uint32(std::get<1>(fetch)));
                for (uint32 questId : ownQuests)
                    handInQuests += (handInQuests.empty() ? "" : ",") + std::to_string(questId);
            }
        }
    }

    if (turnInParking)
        SET_AI_VALUE2(std::string, "manual string", HandInQuestsValue, handInQuests);

    // TEMPORARY probe. 20529 finished quests sit unhanded-in across the bot
    // population and 786 bots have a full quest log, yet the log records only a
    // handful of journeys to a quest taker. The destination is built here, so
    // this records what was on offer; which one then won is already written to
    // bot_events.csv by setNewTarget. Limited to the pinned bots, since a
    // thousand of them would drown the log. Remove once the answer is in.
    if (sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions && UsesQuestFirstProgression(bot))
    {
        uint32 takers = 0, objectives = 0, givers = 0, readyToHandIn = 0;
        for (auto& [purpose, questId, range] : destinationFetches)
        {
            if (purpose & (uint32)TravelDestinationPurpose::QuestTaker) takers++;
            else if (purpose & (uint32)TravelDestinationPurpose::QuestGiver) givers++;
            else objectives++;
        }

        for (auto& [questId, questStatus] : bot->getQuestStatusMap())
            if (!questStatus.m_rewarded && questStatus.m_status == QUEST_STATUS_COMPLETE)
                readyToHandIn++;

        sLog.outBasic("[QuestFirstRoute] state=request bot=%u level=%u source_map=%u source_zone=%u finished=%u active=%u "
            "offered_takers=%u objectives=%u givers=%u strategy=%s parked=%u",
            bot->GetGUIDLow(), bot->GetLevel(), bot->GetMapId(),
            sTerrainMgr.GetZoneId(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()), readyToHandIn,
            uint32(bot->getQuestStatusMap().size()), takers, objectives, givers, event.getSource().c_str(),
            parkTarget->ParkedTurnInCount());
    }

    *AI_VALUE(FutureDestinations*, "future travel destinations") = std::async(std::launch::async,
        [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, destinationFetches, hubsFirst = searchStage > 0]()
        {
            PartitionedTravelList list;
            for (auto [purpose, questId, range] : destinationFetches)
            {
                PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo, purpose, { questId }, true, range);

                for (auto& [partition, points] : subList)
                    list[partition].insert(list[partition].end(), points.begin(), points.end());
            }

            // #421 B: widened search - quest hubs first (in this job, not on the map thread).
            if (hubsFirst)
                SortQuestHubsFirst(list);

            if (list.empty())
                list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::QuestGiver);

            return list;
        }
    );

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", "quest");
    SET_AI_VALUE2(std::string, "manual string", "future travel condition", event.getSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestQuestTravelTargetAction::isAllowed() const
{
    if (AI_VALUE(bool, "should get money"))
        return urand(1, 100) < 90;
    else
        return urand(1, 100) < 95;

    return false;
}

bool FocusTravelTargetAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();

    if (text == "?")
    {
        std::set<uint32> questIds = AI_VALUE(focusQuestTravelList, "focus travel target");
        std::ostringstream out;
        if (questIds.empty())
            out << "No quests selected.";
        else
        {
            out << "I will try to only do the following " << questIds.size() << " quests:";

            for (auto questId : questIds)
            {
                const Quest* quest = sObjectMgr.GetQuestTemplate(questId);

                if (quest)
                    out << ChatHelper::formatQuest(quest);
            }

        }
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    std::set<uint32> questIds = ChatHelper::ExtractAllQuestIds(text);

    if (questIds.empty() && !text.empty())
    {
        if (Qualified::isValidNumberString(text))
            questIds.insert(stoi(text));
        else
        {
            std::vector<std::string> qualifiers = Qualified::getMultiQualifiers(text, ",");

            for (auto& qualifier : qualifiers)
                if (Qualified::isValidNumberString(qualifier))
                    questIds.insert(stoi(text));
        }
    }

    SET_AI_VALUE(focusQuestTravelList, "focus travel target", questIds);

    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT))
        ai->TellError(requester, "travel strategy disabled bot needs this to actually do the quest.");

    if (!ai->HasStrategy("rpg quest", BotState::BOT_STATE_NON_COMBAT))
        ai->TellError(requester, "rpg quest strategy disabled bot needs this to actually do the quest.");

    std::ostringstream out;
    if (questIds.empty())
        out << "I will now do all quests.";
    else
    {
        out << "I will now only try to do the following " << questIds.size() << " quests:";

        for (auto questId : questIds)
        {
            const Quest* quest = sObjectMgr.GetQuestTemplate(questId);

            if (quest)
                out << ChatHelper::formatQuest(quest);
        }

    }
    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    oldTarget->SetExpireIn(1000);
    
    return true;
}
