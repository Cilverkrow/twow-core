#include "playerbot/playerbot.h"
#include "playerbot/DirectTargetDestinations.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/StartupTravelPolicy.h"
#include "playerbot/strategy/triggers/ProfessionUseTriggers.h"
#include "playerbot/strategy/values/CraftValues.h"

#include <bitset>
#include <typeinfo>

using namespace ai;
using namespace ai::direct_targets;

namespace
{
// Per-bot state in manual values: map thread of the bot only, no lock.
char const* const TripValue = "direct target trip";              // Kind + 1, 0 = no open trip
char const* const TripKeyValue = "direct target trip key";
char const* const TripStartValue = "direct target trip start";
char const* const TripArrivedValue = "direct target trip arrived";
char const* const TripCraftsValue = "direct target trip crafts";
char const* const StationMaskValue = "direct target station mask";
char const* const StationMaskAtValue = "direct target station mask at";

char const* CooldownValue(Kind kind)
{
    switch (kind)
    {
        case Kind::Station:
        case Kind::Cooking: return "direct target next station";
        case Kind::Dungeon: return "direct target next dungeon";
        case Kind::Raid: return "direct target next raid";
        default: return nullptr;
    }
}

// The request purpose of a class: its need value is the trip's travel condition.
TravelDestinationPurpose PurposeOf(Kind kind)
{
    switch (kind)
    {
        case Kind::Station:
        case Kind::Cooking: return TravelDestinationPurpose::Station;
        case Kind::Dungeon: return TravelDestinationPurpose::DungeonEntrance;
        case Kind::Raid: return TravelDestinationPurpose::RaidEntrance;
        default: return TravelDestinationPurpose::GatherFishing;
    }
}

// Written once at boot by the loader (Master thread, before the world thread), read-only afterwards.
std::bitset<256> dungeonLevels;

// A finished quest the bot can hand in: gathering yields to it (hotfix 8.5, TravelValues.cpp), so do stations and
// entrances. With turn-in parking on only an open (not parked) turn-in counts, as there.
bool TurnInWaiting(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    TravelTarget const* turnIns = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures ? AI_VALUE(TravelTarget*, "travel target") : nullptr;
    uint32 finished = 0;
    for (auto const& [questId, status] : bot->getQuestStatusMap())
        if (!status.m_rewarded && status.m_status == QUEST_STATUS_COMPLETE && (!turnIns || turnIns->IsTurnInOpen(questId)))
            ++finished;
    return ai::quest_search::GatherYieldsToTurnIn(true, finished);
}

bool HasRoomForProduct(SpellEntry const* spell, Player* bot)
{
    ItemPosCountVec dest;
    return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, spell->EffectItemType[0], 1) == EQUIP_ERR_OK;
}

// The bot's own trip of this class (or one sharing its cooldown) is open and its travel target still on it.
bool OwnTripOpen(PlayerbotAI* ai, Kind kind)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    int32 const trip = AI_VALUE2(int, "manual int", TripValue);
    if (trip <= 0 || !SharesCooldown(Kind(trip - 1), kind))
        return false;

    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    if (!target || !target->GetDestination() || target->GetDestination()->GetEntry() != AI_VALUE2(int, "manual int", TripKeyValue))
        return false;
    TravelStatus const status = target->GetStatus();
    return status == TravelStatus::TRAVEL_STATUS_READY || status == TravelStatus::TRAVEL_STATUS_TRAVEL ||
        status == TravelStatus::TRAVEL_STATUS_WORK;
}

// The class cooldown, except during the bot's own open trip of that class (the need is the trip's travel condition).
// The trip is only looked up while a cooldown runs.
bool NeedCooldownRunning(PlayerbotAI* ai, Kind kind, uint32 now)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 const until = uint32(std::max(0, AI_VALUE2(int, "manual int", CooldownValue(kind))));
    return CooldownRunning(now, until) && CooldownBlocksNeed(now, until, OwnTripOpen(ai, kind));
}

bool EntranceNeed(PlayerbotAI* ai, Kind kind)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    uint32 const now = uint32(time(nullptr));
    if (NeedCooldownRunning(ai, kind, now))
        return false;

    // Only when the quest search really has nothing: no running 8.1 route backoff (an entrance trip would hold the
    // next quest request back) and no open turn-in.
    if (now < uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until"))))
        return false;
    if (TurnInWaiting(ai))
        return false;

    if (kind == Kind::Raid)
        return bot->GetLevel() == sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    return DungeonLevelCovered(bot->GetLevel());
}

void EndTrip(PlayerbotAI* ai, char const* result)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    int32 const trip = AI_VALUE2(int, "manual int", TripValue);
    if (trip <= 0)
        return;

    uint32 const now = uint32(time(nullptr));
    uint32 const start = uint32(std::max(0, AI_VALUE2(int, "manual int", TripStartValue)));
    Kind const kind = Kind(trip - 1);
    sLog.outBasic("[DirectTarget] state=trip_end class=%s bot=%u seconds=%u result=%s key=%d crafts=%d",
        KindName(kind), ai->GetBot()->GetGUIDLow(), start && now > start ? now - start : 0u, result,
        AI_VALUE2(int, "manual int", TripKeyValue), AI_VALUE2(int, "manual int", TripCraftsValue));
    SET_AI_VALUE2(int, "manual int", TripValue, 0);

    // Nothing of the trip carries over: the shore point stays no "fish spot" of a later fishing trip, a station pick that
    // the craft trigger did not see used up is no craft of the next station trip, and a need cached during the trip (the
    // cooldown ignored) does not request the class again at once.
    if (kind == Kind::Fishing)
        RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
    else
    {
        SET_AI_VALUE2(int, "manual int", "station craft pick", 0);
        context->ClearValues("need travel purpose::" + std::to_string(uint32(PurposeOf(kind))));
    }
}

// The end of the open trip: crafted / arrived, else how it ended without arriving.
void CloseTrip(PlayerbotAI* ai, char const* otherwise)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    EndTrip(ai, TripResult(AI_VALUE2(int, "manual int", TripArrivedValue) > 0,
        uint32(std::max(0, AI_VALUE2(int, "manual int", TripCraftsValue))), otherwise));
}

// An event of the destination of the open trip (not of a group member's copy or an older target).
bool IsTripDestination(PlayerbotAI* ai, TravelDestination const* destination)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    return destination && AI_VALUE2(int, "manual int", TripValue) > 0 &&
        AI_VALUE2(int, "manual int", TripKeyValue) == destination->GetEntry();
}
}

// ---- Destinations -----------------------------------------------------------------------------------------------------

StationTravelDestination::StationTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry)
    : EntryTravelDestination(purpose, entry), DirectTargetTag(KindOfFocus(StationFocusOf(entry)))
{
    SetRadiusMin(StationRadius);
    SetTeamMask(StationTeamOf(entry));
}

bool StationTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    return TeamAllowed(GetTeamMask(), info.GetTeam() == ALLIANCE);
}

bool StationTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = GetBotAI(bot);
    if (!ai || !IsLive(GetKind()) || IsDisabled() || !IsPossible(info))
        return false;

    WorldPosition const botPos(bot);
    if (!OnMap(botPos) || DistanceTo(botPos) > StationMaxDistance)
        return false;

    return (StationRecipeMask(ai) & FocusBit(GetFocusId())) != 0;
}

std::string StationTravelDestination::GetTitle() const
{
    switch (GetFocusId())
    {
        case FocusAnvil: return "use an anvil";
        case FocusForge: return "use a forge";
        case FocusCooking: return "cook at a fire";
        default: return "use a crafting station";
    }
}

bool ShoreFishTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!TeamAllowed(GetTeamMask(), info.GetTeam() == ALLIANCE))
        return false;

    return FishingSkillOk(info.GetCurrentSkill(SKILL_FISHING), reqSkill, info.GetSkillMax(SKILL_FISHING));
}

bool ShoreFishTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = GetBotAI(bot);
    if (!ai || !IsLive(Kind::Fishing) || IsDisabled())
        return false;

    if (!GatherTravelDestination::IsActive(bot, info))
        return false;

    // The common gate (design section 3): the GatherFishing need says yes to every bot with tfish that is not a roster bot on
    // its own, so the shore points would reach bots with a real master and non-roster bots.
    if (!IsRosterBotOnItsOwn(ai))
        return false;

    // The GatherFishing need does not look for a pole (review R2).
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(bool, "can fish");
}

EntranceTravelDestination::EntranceTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry)
    : EntryTravelDestination(purpose, entry),
    DirectTargetTag(purpose == TravelDestinationPurpose::RaidEntrance ? Kind::Raid : Kind::Dungeon)
{
    SetExpireFast();
    SetCooldownLong();
    SetRadiusMin(EntranceRadius);
}

bool EntranceTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!TeamAllowed(GetTeamMask(), info.GetTeam() == ALLIANCE))
        return false;

    if (GetKind() == Kind::Raid)
        return RaidLevelFit(info.GetLevel(), maxPlayerLevel, requiredLevel, true);
    return DungeonLevelFit(info.GetLevel(), levelMin, levelMax, requiredLevel);
}

bool EntranceTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    if (!IsLive(GetKind()) || IsDisabled() || requiredPhase > sWorld.GetContentPhase())
        return false;

    WorldPosition const botPos(bot);
    if (!OnMap(botPos) || DistanceTo(botPos) > EntranceMaxDistance)
        return false;

    return IsPossible(info);
}

std::string EntranceTravelDestination::GetTitle() const
{
    MapEntry const* map = sMapStore.LookupEntry(GetInstanceMapId());
    return std::string("the entrance of ") + (map && map->name ? map->name : "an instance");
}

// ---- Switches ---------------------------------------------------------------------------------------------------------

uint32 ai::direct_targets::ConfiguredMask()
{
    return SwitchMask(sPlayerbotAIConfig.directTargetsEnabled, sPlayerbotAIConfig.directTargetsStation,
        sPlayerbotAIConfig.directTargetsCooking, sPlayerbotAIConfig.directTargetsFishing,
        sPlayerbotAIConfig.directTargetsDungeon, sPlayerbotAIConfig.directTargetsRaid);
}

bool ai::direct_targets::IsLive(Kind kind)
{
    return AnyBuilt() && Live(ConfiguredMask(), kind);
}

bool ai::direct_targets::PurposeLive(TravelDestinationPurpose purpose)
{
    switch (purpose)
    {
        case TravelDestinationPurpose::Station: return IsLive(Kind::Station) || IsLive(Kind::Cooking);
        case TravelDestinationPurpose::DungeonEntrance: return IsLive(Kind::Dungeon);
        case TravelDestinationPurpose::RaidEntrance: return IsLive(Kind::Raid);
        default: return false;
    }
}

bool ai::direct_targets::StationCraftLive()
{
    return PurposeLive(TravelDestinationPurpose::Station);
}

DirectTargetTag* ai::direct_targets::TagOf(TravelDestination const* destination)
{
    if (!destination || !IsDirectKey(destination->GetEntry()))
        return nullptr;
    return dynamic_cast<DirectTargetTag*>(const_cast<TravelDestination*>(destination));
}

// ---- Need -------------------------------------------------------------------------------------------------------------

bool ai::direct_targets::NeedPurpose(PlayerbotAI* ai, TravelDestinationPurpose purpose)
{
    uint32 const mask = ConfiguredMask();
    switch (purpose)
    {
        case TravelDestinationPurpose::Station:
        {
            if ((!Live(mask, Kind::Station) && !Live(mask, Kind::Cooking)) || !IsRosterBotOnItsOwn(ai))
                return false;

            if (NeedCooldownRunning(ai, Kind::Station, uint32(time(nullptr))))
                return false;
            // Like gathering: an open turn-in first (GatherYieldsToTurnIn), also during the trip.
            if (TurnInWaiting(ai))
                return false;
            return StationRecipeMask(ai) != 0;
        }
        case TravelDestinationPurpose::DungeonEntrance:
            return Live(mask, Kind::Dungeon) && IsRosterBotOnItsOwn(ai) && EntranceNeed(ai, Kind::Dungeon);
        case TravelDestinationPurpose::RaidEntrance:
            return Live(mask, Kind::Raid) && IsRosterBotOnItsOwn(ai) && EntranceNeed(ai, Kind::Raid);
        default:
            return false;
    }
}

bool ai::direct_targets::StationRecipeUsable(PlayerbotAI* ai, SpellEntry const* spell, time_t now)
{
    Player* bot = ai->GetBot();
    if (!spell || !ShouldCraftSpellValue::SpellGivesSkillUp(spell->Id, bot))
        return false;
    if (!HasCraftTools(spell, bot) || !CraftableFromBags(spell, bot))
        return false;

    // The backoff rule of ProfessionCraftTrigger: a recipe that failed for good waits out its backoff, one on (category)
    // cooldown or without room for its product waits until that changed.
    AiObjectContext* context = ai->GetAiObjectContext();
    bool const failedBackoff = now < AI_VALUE2(time_t, "manual time", "profession craft failed until");
    bool const backedOff = (failedBackoff && AI_VALUE2(int, "manual int", "profession craft failed spell") == int32(spell->Id)) ||
        bot->HasSpellCooldown(spell->Id) || (spell->Category && bot->HasSpellCategoryCooldown(spell->Category)) ||
        !HasRoomForProduct(spell, bot);
    return !backedOff;
}

uint32 ai::direct_targets::StationRecipeMask(PlayerbotAI* ai)
{
    // ProfessionUse.Craft and RealReagents: the station craft casts from the bags; the craft node lives in "gather".
    if (!sPlayerbotAIConfig.professionUseCraft || !sPlayerbotAIConfig.professionUseRealReagents ||
        !ai->HasStrategy("gather", BotState::BOT_STATE_NON_COMBAT))
        return 0;

    uint32 const allowed = (IsLive(Kind::Station) ? FocusBit(FocusAnvil) | FocusBit(FocusForge) : 0) |
        (IsLive(Kind::Cooking) ? FocusBit(FocusCooking) : 0);
    if (!allowed)
        return 0;

    AiObjectContext* context = ai->GetAiObjectContext();
    time_t const now = time(nullptr);
    uint32 const at = uint32(std::max(0, AI_VALUE2(int, "manual int", StationMaskAtValue)));
    if (at && uint32(now) < at + NeedCacheSeconds)
        return uint32(std::max(0, AI_VALUE2(int, "manual int", StationMaskValue))) & allowed;

    uint32 mask = 0;
    for (uint32 spellId : AI_VALUE(std::vector<uint32>, "craft spells"))
    {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
        uint32 const bit = spell ? FocusBit(spell->RequiresSpellFocus) : 0;
        if (!bit || !(bit & allowed) || (mask & bit))
            continue;
        if (StationRecipeUsable(ai, spell, now))
            mask |= bit;
    }

    SET_AI_VALUE2(int, "manual int", StationMaskValue, int32(mask));
    SET_AI_VALUE2(int, "manual int", StationMaskAtValue, int32(now));
    return mask;
}

void ai::direct_targets::NoteDungeonLevels(uint32 minLevel, uint32 maxLevel)
{
    for (uint32 level = minLevel; level <= maxLevel && level < dungeonLevels.size(); ++level)
        dungeonLevels.set(level);
}

bool ai::direct_targets::DungeonLevelCovered(uint32 level)
{
    return level < dungeonLevels.size() && dungeonLevels.test(level);
}

// ---- Runtime hooks ----------------------------------------------------------------------------------------------------

void ai::direct_targets::NoteEmpty(PlayerbotAI* ai, std::string const& purpose)
{
    uint32 slot = EmptySlots;
    if (purpose == std::to_string(uint32(TravelDestinationPurpose::Station)))
        slot = 0;
    else if (purpose == std::to_string(uint32(TravelDestinationPurpose::DungeonEntrance)))
        slot = 1;
    else if (purpose == std::to_string(uint32(TravelDestinationPurpose::RaidEntrance)))
        slot = 2;
    if (slot >= EmptySlots)
        return;
    Kind const kind = slot == 0 ? Kind::Station : (slot == 1 ? Kind::Dungeon : Kind::Raid);

    Stats().empty[slot].fetch_add(1, std::memory_order_relaxed);
    // Nothing in reach (stations within 1000 yd, entrances within 1500 yd): not again with the next target expiry.
    AiObjectContext* context = ai->GetAiObjectContext();
    SET_AI_VALUE2(int, "manual int", CooldownValue(kind), int32(uint32(time(nullptr)) + EmptyCooldownSeconds));
}

void ai::direct_targets::OnChoice(PlayerbotAI* ai, TravelTarget& target)
{
    // A trip that nothing else ended ends with the next choice.
    CloseTrip(ai, "replaced");

    TravelDestination* destination = target.GetDestination();
    DirectTargetTag* tag = TagOf(destination);
    if (!tag)
        return;

    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    Kind const kind = tag->GetKind();
    uint32 const now = uint32(time(nullptr));
    SET_AI_VALUE2(int, "manual int", "station craft pick", 0);

    int32 chance = 0;
    uint32 dest = 0;
    if (ShoreFishTravelDestination* fish = dynamic_cast<ShoreFishTravelDestination*>(destination))
    {
        dest = fish->GetAreaId();
        chance = FishingChance(int32(bot->GetSkillValue(SKILL_FISHING)), fish->GetReqSkill());
        // The final approach of MoveToFishAction goes to the stored "fish spot" first (review R2): the chosen shore
        // point, facing the hole, replaces a spot left over from an earlier trip.
        if (target.GetPosition())
            SET_AI_VALUE2(WorldPosition, "custom position", "fish spot", *target.GetPosition());
    }
    else if (StationTravelDestination* station = dynamic_cast<StationTravelDestination*>(destination))
        dest = station->GetZoneId();
    else if (EntranceTravelDestination* entrance = dynamic_cast<EntranceTravelDestination*>(destination))
        dest = entrance->GetInstanceMapId();

    sLog.outBasic("[DirectTarget] state=pick class=%s bot=%u level=%u zone=%u dest=%u distance=%.0f key=%d chance=%d",
        KindName(kind), bot->GetGUIDLow(), bot->GetLevel(), bot->GetZoneId(), dest,
        target.GetPosition() ? target.Distance(bot) : -1.0f, destination->GetEntry(), chance);
    Stats().picks[uint32(kind)].fetch_add(1, std::memory_order_relaxed);

    if (char const* cooldown = CooldownValue(kind))
        SET_AI_VALUE2(int, "manual int", cooldown, int32(now + CooldownSeconds(kind)));

    SET_AI_VALUE2(int, "manual int", TripValue, int32(kind) + 1);
    SET_AI_VALUE2(int, "manual int", TripKeyValue, destination->GetEntry());
    SET_AI_VALUE2(int, "manual int", TripStartValue, int32(now));
    SET_AI_VALUE2(int, "manual int", TripArrivedValue, 0);
    SET_AI_VALUE2(int, "manual int", TripCraftsValue, 0);
}

void ai::direct_targets::OnArrive(TravelDestination* destination, PlayerbotAI* ai, WorldPosition const* position)
{
    // Switched off by a reload: no arrival is counted or logged (an open trip still logs its end, pairing its pick).
    DirectTargetTag* tag = TagOf(destination);
    if (!tag || !IsLive(tag->GetKind()))
        return;

    // A station counts as reached with its first craft (OnStationCraft): one the bots get to but cannot craft at
    // (no_focus) has to fail as well.
    Kind const kind = tag->GetKind();
    if (kind != Kind::Station && kind != Kind::Cooking)
        tag->NoteArrival();
    if (!IsTripDestination(ai, destination))
        return;

    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    SET_AI_VALUE2(int, "manual int", TripArrivedValue, 1);

    if (kind != Kind::Fishing)
    {
        sLog.outBasic("[DirectTarget] state=arrive bot=%u class=%s key=%d", bot->GetGUIDLow(), KindName(tag->GetKind()),
            destination->GetEntry());
        return;
    }

    // The point the bot arrived at (a travel refresh may have moved it from the one chosen): MoveToFishAction walks to
    // the stored "fish spot" first.
    if (position)
        SET_AI_VALUE2(WorldPosition, "custom position", "fish spot", *position);

    // Most holes are pool members and despawned at a given time: the shore point is fishable water near a hole spawn
    // site. Logged, the metric is catches per cast.
    uint32 holePresent = 0;
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los"))
    {
        GameObject* go = ai->GetGameObject(guid);
        if (go && go->GetGOInfo() && go->GetGOInfo()->type == GAMEOBJECT_TYPE_FISHINGHOLE && go->IsWithinDist(bot, 30.0f))
        {
            holePresent = 1;
            break;
        }
    }
    sLog.outBasic("[DirectTarget] state=arrive bot=%u class=%s key=%d hole_present=%u", bot->GetGUIDLow(),
        KindName(tag->GetKind()), destination->GetEntry(), holePresent);
}

void ai::direct_targets::OnTravelFailure(PlayerbotAI* ai, TravelDestination* destination, char const* reason)
{
    DirectTargetTag* tag = TagOf(destination);
    if (!tag)
        return;

    // Switched off by a reload: no failure memory, no disable line; the open trip still ends.
    bool const travelExpiry = std::string(reason) == "expired";
    bool const startupHeld = sPlayerbotAIConfig.startupTravelMaxLongMovesPerSlot &&
        startup_travel::InWindow(sWorld.GetUptime(), sPlayerbotAIConfig.startupTravelWindowSeconds);
    if (IsLive(tag->GetKind()) && CountsAsFailure(travelExpiry, startupHeld))
    {
        uint32 const failures = tag->NoteFailure();
        Stats().fail.fetch_add(1, std::memory_order_relaxed);
        uint32 const arrivals = tag->GetArrivals();
        if (ShouldDisable(failures, arrivals) && tag->Disable(uint32(time(nullptr))))
        {
            Stats().disabled.fetch_add(1, std::memory_order_relaxed);
            sLog.outBasic("[DirectTarget] state=disable key=%d class=%s failures=%u arrivals=%u seconds=%u",
                destination->GetEntry(), KindName(tag->GetKind()), failures, arrivals, DisableSeconds);
        }
    }

    if (IsTripDestination(ai, destination))
        EndTrip(ai, reason);
}

void ai::direct_targets::OnTargetChange(PlayerbotAI* ai, TravelDestination* from, TravelDestination* to)
{
    if (!ai || from == to || !TagOf(from) || !IsTripDestination(ai, from))
        return;

    CloseTrip(ai, (!to || typeid(*to) == typeid(NullTravelDestination)) ? "reset" : "replaced");
}

void ai::direct_targets::OnTargetStatus(PlayerbotAI* ai, TravelDestination* destination, TravelStatus status)
{
    char const* otherwise = status == TravelStatus::TRAVEL_STATUS_COOLDOWN ? "inactive" :
        status == TravelStatus::TRAVEL_STATUS_EXPIRED ? "ended" : status == TravelStatus::TRAVEL_STATUS_NONE ? "reset" : nullptr;
    if (!ai || !otherwise || !TagOf(destination) || !IsTripDestination(ai, destination))
        return;

    CloseTrip(ai, otherwise);
}

void ai::direct_targets::OnStationCraft(PlayerbotAI* ai, TravelDestination* destination)
{
    Stats().crafts.fetch_add(1, std::memory_order_relaxed);
    if (!IsTripDestination(ai, destination))
        return;

    AiObjectContext* context = ai->GetAiObjectContext();
    int32 const crafts = AI_VALUE2(int, "manual int", TripCraftsValue);
    SET_AI_VALUE2(int, "manual int", TripCraftsValue, crafts + 1);
    // The station's arrival for the failure memory: once per trip, with its first craft.
    DirectTargetTag* tag = TagOf(destination);
    if (!crafts && tag && IsLive(tag->GetKind()))
        tag->NoteArrival();
}

bool ai::direct_targets::IsSilentTrip(TravelDestination const* destination)
{
    return destination && IsDirectKey(destination->GetEntry());
}
