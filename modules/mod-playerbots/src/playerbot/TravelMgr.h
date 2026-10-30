#pragma once

#include "ProgressAwareTurnInRecoveryPolicy.h"
#include "QuestTurnInParkPolicy.h"
#include "TransportStallPolicy.h"
#include "DestinationDeathPolicy.h"
#include "CampBlockPolicy.h"
#include "QuestLoopPolicy.h"
#include "AreaLevelPolicy.h"
#include "PosStuckPolicy.h"
#include "QuestSearchPolicy.h"   // twow-repo#601 quest acceptance step 1b: GiverDropCounts
#include <mutex>

#include "strategy/AiObject.h"
#include <boost/functional/hash.hpp>
#include <map>
#include <set>      // twow-repo#601 no-route gate (TravelTarget::noRouteTurnIns)
#include <vector>
#include <string>
#include "GuidPosition.h"
#include "strategy/values/TravelValues.h"
#include "WorldSquare.h"

namespace ai
{
	class GuidePosition;

	class MapTransfer
	{
	public:
		MapTransfer(const WorldPosition& pointFrom, const WorldPosition& pointTo, float portalLength = 0.1f)
			: pointFrom(pointFrom), pointTo(pointTo), portalLength(portalLength) {
		}
		float Distance(const WorldPosition& start, const WorldPosition& end) const { return (isUseful(start, end) ? (start.distance(pointFrom) + portalLength + pointTo.distance(end)) : 200000); }

		float FDist(const WorldPosition& start, const WorldPosition& end) const { return start.fDist(pointFrom) + portalLength + pointTo.fDist(end); }

		float sqDist(const WorldPosition& start, const WorldPosition& end) const { return start.sqDistance2d(pointFrom) + (portalLength * portalLength) + pointTo.sqDistance2d(end); }

		WorldPosition GetPointFrom() const { return pointFrom; }
		WorldPosition GetPointTo() const { return pointTo; }
	private:
		bool isFrom(const WorldPosition& point) const { return point.getMapId() == pointFrom.getMapId(); }
		bool isTo(const WorldPosition& point) const { return point.getMapId() == pointTo.getMapId(); }

		bool isUseful(const WorldPosition& point) const { return isFrom(point) || isTo(point); }
		bool isUseful(const WorldPosition& start, const WorldPosition& end) const { return isFrom(start) && isTo(end); }

		float Distance(const WorldPosition& point) const { return isUseful(point) ? (isFrom(point) ? point.distance(pointFrom) : point.distance(pointTo)) : 200000; }

		WorldPosition pointFrom, pointTo;
		float portalLength = 0.1f;
	};

	class PlayerTravelInfo 
	{
	public:
		PlayerTravelInfo() {};
		PlayerTravelInfo(Player* player, bool gapGrind = false);   // twow-repo#601 S2 R3: gapGrind = a GrindMode 1 gap grind fetch / choice / trip
		
		const WorldPosition& GetPosition() const { return position; }
		Team GetTeam() const { return team; }
		uint32 GetLevel() const  { return level; }
		uint16 GetCurrentSkill(SkillType skillType) const  { return currentSkill[skillType]; }
		uint16 GetSkillMax(SkillType skillType) const { return skillMax[skillType]; }
		bool IsInRaid() const { return groupSize > 5; }
		bool IsInGroup() const { return groupSize > 1 && groupSize <= 5; }
		uint32 GetMoney() const { return money; }

		bool HasFocusQuest() const {return !focusList.empty();}
		bool IsFocusQuest(uint32 questId) const { return focusList.find(questId) != focusList.end(); }

		bool GetBoolValue(const std::string& valueName) const {return boolValues.at(valueName);};
		bool GetBoolValue2(const std::string& valueName, const std::string& qualifier) const { return boolValues.at(valueName + "::" + qualifier); };
		uint8 GetUint8Value(const std::string& valueName) const { return uint8Values.at(valueName);};
		uint8 GetUint32Value(const std::string& valueName) const { return uint32Values.at(valueName); };

		// twow-repo#601 S2 R3 (GrindMode 1, OB-10 rev. 2 point 1): the scope bit - the creature level window of the gap filler's
		// population, set in the constructor on the bot thread and read by the async grind fetch only as a value
		// (GrindTravelDestination::IsPossible, TravelMgr::IsLocationLevelValid). 0 / 0 = no window: every other bot, GrindMode 0 / 2,
		// and every snapshot that is not for a gap grind (review C-F2: a quest objective fetch, choice or trip to grind mobs keeps
		// the old template test and area gate; only the gap grind fetch, its choice and the gap trip pass gapGrind).
		bool HasGapWindow() const { return gapWindowHigh != 0; }
		uint32 GetGapWindowLow() const { return gapWindowLow; }
		uint32 GetGapWindowHigh() const { return gapWindowHigh; }
		// twow-repo#601 S5a (BandLadder, S5A-DESIGN.md 5.4): the band giver filter bit and the grey level, set in the constructor on the
		// bot thread (PlayerbotAI::QuestBandGiverFilter: switch first, then the scope and a usable ladder row for the bot's band);
		// read by QuestRelationTravelDestination::IsPossible / IsActive only as values. false / 0 for every other bot.
		bool HasBandGiverFilter() const { return bandGiverFilter; }
		// twow-repo#333 P1-v (AiPlayerbot.Travel.RpgAreaLevelFromSpawn): the area level of an RPG target from its spawn nearest to the
		// bot (RpgAreaLevelPolicy.h), set in the constructor on the bot thread; false for every bot with the switch off.
		bool UsesRpgAreaFromSpawn() const { return rpgAreaFromSpawn; }
		// twow-repo#601 quest acceptance lever (iv) (QuestFirstProgression.GiverFetchAnyQuest): the lever applies to this bot (a roster
		// bot without a real player master in the mode's cohort), set in the constructor on the bot thread; false with the mode off.
		bool HasGiverAnyQuest() const { return giverAnyQuest; }
		uint32 GetBandGreyLevel() const { return bandGreyLevel; }
		// twow-repo#601 quest acceptance step 1b (TraceTravelDecisions): the drop counters of one traced giver fetch. Set only on the
		// copy the async quest fetch job passes to GetPartitions, counted by QuestRelationTravelDestination::IsPossible in that job's
		// thread and written by it; null everywhere else (no count).
		void SetGiverDrops(ai::quest_search::GiverDropCounts* counts) { giverDrops = counts; }
		ai::quest_search::GiverDropCounts* GetGiverDrops() const { return giverDrops; }

	private:
		WorldPosition position;
		Team team = TEAM_NONE;
		uint32 level = 0;
		uint32 gapWindowLow = 0;    // twow-repo#601 S2 R3: 0 / 0 for every bot but the GrindMode 1 population
		uint32 gapWindowHigh = 0;
		bool bandGiverFilter = false;    // twow-repo#601 S5a: false for every bot but an in-scope BandLadder bot with a usable row
		bool rpgAreaFromSpawn = false;   // twow-repo#333 P1-v: Travel.RpgAreaLevelFromSpawn applies to this bot
		bool giverAnyQuest = false;     // twow-repo#601 lever (iv): QuestFirstProgression.GiverFetchAnyQuest applies to this bot
		uint32 bandGreyLevel = 0;
		uint16 currentSkill[MAX_SKILL_TYPE] = {0};
		uint16 skillMax[MAX_SKILL_TYPE] = { 0 };
		uint8 groupSize = 0;
		uint32 money = 0;
		ai::quest_search::GiverDropCounts* giverDrops = nullptr;   // twow-repo#601 step 1b: only on the traced giver fetch's copy

		focusQuestTravelList focusList = {};

		std::unordered_map<std::string, bool> boolValues = 
		{
			{"should get money", false},
			{"should sell", false},
			{"can sell", false},
			{"should repair", false},
			{"can repair", false},
			{"should ah sell", false},
			{"can ah sell", false},
			{"can get mail", false },
			{"has strategy::rpg quest", false},
			{"can fight equal", false},
			{"can fight elite", false},
			{"can fight boss", false},
		};

		std::unordered_map<std::string, uint8> uint8Values =
		{
			{"free quest log slots", 0},
			{"durability",0},
		};

		std::unordered_map<std::string, uint32> uint32Values =
		{
			{"death count", 0},
		};
	};
	
	//A destination for a bot to travel to and do something.
	class TravelDestination : public WorldWpSquare
	{
	public:
		TravelDestination() {}

		//void AddPoint(WorldPosition* pos) { points.push_back(pos); }

		virtual std::string GetTitle() const { return "generic travel destination"; }

		uint32 GetExpireDelay() { return expireDelay; }
		uint32 GetCooldownDelay() { return cooldownDelay; }

		virtual bool IsIn(const WorldPosition& pos, float radius = 0) const { return OnMap(pos) && DistanceTo(pos) <= (radius ? radius : radiusMin); }
		float DistanceTo(const WorldPosition& pos) const { return sqrt(sqDistance(pos)); }
		// twow-repo#627 S7: the destination has points on the map of pos (OnMap is protected in WorldWpSquare).
		bool HasPointsOnMap(const WorldPosition& pos) const { return OnMap(pos); }

		float GetRadiusMin() { return radiusMin; }

		virtual bool IsPossible(const PlayerTravelInfo& info) const { return false; }
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const { return false; }

		virtual int32 GetEntry() const { return 0; }
		virtual TravelDestinationPurpose GetPurpose() const { return TravelDestinationPurpose::None; }
		// twow-repo#601 S5a (BandLadder): true only for the boot-built band hubs (QuestBand.h).
		virtual bool IsBandHub() const { return false; }

		virtual std::string GetShortName() const { return ""; };

		// twow-repo#575 (PATH-01 E4/E5, AiPlayerbot.CustomRaceStartZones): a destination whose points all lie in the goblin
		// or high-elf start zone is a target for that faction only (TravelMgr::GetDestinations). 0 = any faction.
		void SetStartZoneTeam(uint8 team) { startZoneTeam = team; }
		uint8 GetStartZoneTeam() const { return startZoneTeam; }
	protected:
		void SetExpireFast() { expireDelay = 60000; } //1 minute
		void SetCooldownShort() { cooldownDelay = 1000; } //1 second
		void SetCooldownLong() { cooldownDelay = 300000; } //5 minutes
		// twow-repo#594 / P3-b: the arrival radius of the direct-target destinations (station 2, entrance 3 yd); the default stays
		// INTERACTION_DISTANCE and no other destination calls it.
		void SetRadiusMin(float r) { radiusMin = r; }
		virtual std::vector<std::string> GetTravelConditions() const { return {}; }

		virtual bool IsOut(const WorldPosition& pos, float radius = 0) const { return !OnMap(pos) || DistanceTo(pos) > (radius ? radius : radiusMax); }
	private:
		//bool OnMap(const WorldPosition& pos) const { return NearestPoint(pos)->getMapId() == pos.getMapId(); }

		//std::vector<WorldPosition*> points;
		float radiusMin = INTERACTION_DISTANCE;
		float radiusMax = sPlayerbotAIConfig.sightDistance;

		uint32 expireDelay = 300000; //5 minutes
		uint32 cooldownDelay = 60000; //1 minute
		uint8 startZoneTeam = 0;  // ai::start_zone::StartTeam
	};

	//A travel target that is always inactive and jumps to cooldown.
	class NullTravelDestination : public TravelDestination
	{
	public:
		NullTravelDestination() : TravelDestination() { SetCooldownLong(); };

		virtual std::string GetTitle() const override { return "no destination"; }
		
		virtual bool IsIn(const WorldPosition& pos, float radius = 0) const override { return true; }

		virtual std::string GetShortName() const override { return "idle"; };
	};

	class TemporaryTravelDestination : public TravelDestination
    {
    public:
        TemporaryTravelDestination(WorldPosition destination) : TravelDestination(), destination(destination) {};

        virtual std::string GetTitle() const override { return destination.print(); }

        virtual std::string GetShortName() const override { return "temp"; };

		WorldPosition* GetPosition() { return &destination; }
        void SetPosition(WorldPosition newDestination) { destination = newDestination; }
    private:
        WorldPosition destination;
    };

	class EntryTravelDestination : public TravelDestination
	{
	public:
		EntryTravelDestination(TravelDestinationPurpose purpose, int32 entry) : TravelDestination(), purpose(purpose), entry(entry) { if (entry > 0) creatureInfo = sObjectMgr.GetCreatureTemplate(entry); else goInfo = sObjectMgr.GetGameObjectInfo(-1 * entry); }
		virtual int32 GetEntry() const override { return entry; }
		virtual GameObjectInfo const* GetGoInfo() const { return goInfo; }
		virtual CreatureInfo const* GetCreatureInfo() const { return creatureInfo; }
		virtual TravelDestinationPurpose GetPurpose() const override { return purpose; }
		bool HasNpcFlag(uint32 flag) { if(GetCreatureInfo() && (GetCreatureInfo()->NpcFlags & flag)) return true; return false; }

		virtual std::string GetShortName() const override;
	private:
		CreatureInfo const* creatureInfo = nullptr;
		GameObjectInfo const* goInfo = nullptr;
		int32 entry = 0;
		TravelDestinationPurpose purpose = TravelDestinationPurpose::None;
	};

	//A travel target specifically related to a quest.
	class QuestTravelDestination : public EntryTravelDestination
	{
	public:
		QuestTravelDestination(TravelDestinationPurpose purpose, uint32 questId, int32 entry) : EntryTravelDestination(purpose, entry), questId(questId) { questTemplate = sObjectMgr.GetQuestTemplate(questId); };

		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override { return bot->IsActiveQuest(questId); }

		virtual std::string GetTitle() const override;

		virtual bool IsClassQuest() const { return questTemplate->GetRequiredClasses(); }
		virtual uint32 GetQuestId() const { return questId; }
	protected:
		virtual Quest const* GetQuestTemplate() const { return questTemplate; }
	private:
		uint32 questId;
		Quest const* questTemplate;
	};

	//A quest giver or taker.
	class QuestRelationTravelDestination : public QuestTravelDestination
	{
	public:
		QuestRelationTravelDestination(TravelDestinationPurpose purpose, uint32 questId, int32 entry) : QuestTravelDestination(purpose, questId, entry) {}

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override;

		virtual uint8 GetRelation() const { return GetPurpose() == TravelDestinationPurpose::QuestTaker; }
	};

	//A quest objective (creature/gameobject to grind/loot)
	class QuestObjectiveTravelDestination : public QuestTravelDestination
	{
	public:
		QuestObjectiveTravelDestination(TravelDestinationPurpose purpose, uint32 questId, int32 entry) : QuestTravelDestination(purpose, questId, entry) { SetExpireFast(); };

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override;

		virtual uint8 GetObjective() const;
	};

	//A location with rpg target(s) based on race and level
	class RpgTravelDestination : public EntryTravelDestination
	{
	public:
		RpgTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry) : EntryTravelDestination(purpose, entry) {}

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override;
	};

	//A location with zone
	class ZoneTravelDestination : public EntryTravelDestination
	{
	public:
		ZoneTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry) : EntryTravelDestination(purpose, entry) {
			SetExpireFast(); SetCooldownShort(); if (auto area = GetArea()) { title = area->area_name[0]; level = area->area_level; }
		}
	protected:
		virtual std::string GetZoneName() const { return title; }
		AreaTableEntry const* GetArea() const;
		int32 GetLevel() const { return level; }
	private:
		std::string title = "";
		int32 level = 0;
	};

	//A location with zone exploration target(s) 
	class ExploreTravelDestination : public ZoneTravelDestination
	{
	public:
		ExploreTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry) : ZoneTravelDestination(purpose, 0, entry) {}

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override { return GetZoneName(); }
	};

	//A location with zone exploration target(s) 
	class GrindTravelDestination : public EntryTravelDestination
	{
	public:
		GrindTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry) : EntryTravelDestination(purpose, entry) {}

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override;
	};

	//A location with a boss
	class BossTravelDestination : public EntryTravelDestination
	{
	public:
		BossTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry) : EntryTravelDestination(purpose, entry) { SetCooldownShort(); }

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override;
	};

	//A location with a object that can be gathered
	class GatherTravelDestination : public EntryTravelDestination
	{
	public:
		GatherTravelDestination() : EntryTravelDestination(TravelDestinationPurpose::GatherFishing, 0) {}
		GatherTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry) : EntryTravelDestination(purpose, entry) {}

		virtual bool IsPossible(const PlayerTravelInfo& info) const override;
		virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
		virtual std::string GetTitle() const override;
	};

	enum class TravelState : uint8
	{
		TRAVEL_STATE_IDLE = 0,
		TRAVEL_STATE_TRAVEL_PICK_UP_QUEST = 1,
		TRAVEL_STATE_WORK_PICK_UP_QUEST = 2,
		TRAVEL_STATE_TRAVEL_DO_QUEST = 3,
		TRAVEL_STATE_WORK_DO_QUEST = 4,
		TRAVEL_STATE_TRAVEL_HAND_IN_QUEST = 5,
		TRAVEL_STATE_WORK_HAND_IN_QUEST = 6,
		TRAVEL_STATE_TRAVEL_RPG = 7,
		TRAVEL_STATE_TRAVEL_EXPLORE = 8,
		MAX_TRAVEL_STATE
	};

	enum class TravelStatus : uint8
	{
		TRAVEL_STATUS_NONE = 0,
		TRAVEL_STATUS_PREPARE = 1,
		TRAVEL_STATUS_READY = 2,
		TRAVEL_STATUS_TRAVEL = 3,
		TRAVEL_STATUS_WORK = 4,
		TRAVEL_STATUS_COOLDOWN = 5,
		TRAVEL_STATUS_EXPIRED = 6,
		MAX_TRAVEL_STATUS
	};

	//Current target and location for the bot to travel to.
	//The flow is as follows:
	//PREPARE   (wait until no loot is near)
	//TRAVEL    (move towards target until close enough) (rpg and grind is disabled)
	//WORK      (grind/rpg until the target is no longer active) (rpg and grind is enabled on quest mobs)
	//COOLDOWN  (wait some time free to do what the bot wants)
	//EXPIRE    (if any of the above actions take too long pick a new target)
	// twow-repo#601 turn_in hold (QuestLoop.RelationWorkNoGrind): a giver / taker destination whose own quest this bot can still
	// take (giver) or hand in (taker). False for every other destination.
	bool QuestRelationOpen(Player* bot, TravelDestination const* destination);

	class TravelTarget : AiObject
	{
	public:
		TravelTarget(PlayerbotAI* ai);
		TravelTarget(PlayerbotAI* ai, TravelDestination* tDestination1, WorldPosition* wPosition1) : AiObject(ai) { SetTarget(tDestination1, wPosition1); }
		~TravelTarget() = default;

		float Distance(Player* bot) const { WorldPosition pos(bot);  return wPosition->distance(pos); };
		TravelDestination* GetDestination() const { return tDestination; };
		WorldPosition* GetPosition() const { return wPosition; };
		std::string GetPosStr() const { return wPosition->to_string(); }

		int32 GetEntry() const { if (!tDestination) return 0; return tDestination->GetEntry(); }
		TravelStatus GetStatus() const { return m_status; }
		TravelState GetTravelState();
		GuidPosition GetGroupmember() { return groupMember; }

		bool IsGroupCopy() const { return groupMember; }
		void SetRelevance(uint32 rel) { relevance = rel; }
		uint32 GetRelevance() { return relevance; }


		bool IsForced() const { return forced; }

		bool IsConditionsActive(bool clear = false);
		bool IsDestinationActive();

		void CheckStatus();

		bool IsActive();

		uint32 GetRetryCount(bool isMove) const { return isMove ? moveRetryCount : extendRetryCount; }
		int32 GetTimeLeft() const { return statusTime - GetExpiredTime(); }
		int32 GetExpiredTime() const { return WorldTimer::getMSTime() - startTime; }

		void SetRetry(bool isMove, uint32 newCount = 0) { if (isMove) moveRetryCount = newCount; else extendRetryCount = newCount; }
		bool IsMaxRetry(bool isMove) { return isMove ? (moveRetryCount > 10) : (extendRetryCount >= 5); }

		void SetTarget(TravelDestination* tDestination1, WorldPosition* wPosition1);
		
		void AddCondition(std::string condition) { travelConditions.push_back(condition); }
		void SetConditions(std::vector<std::string> conditions) { travelConditions = conditions; }
		std::vector<std::string> GetConditions() { return travelConditions; }

		void SetStatus(TravelStatus status);
		void SetExpireIn(uint32 expireMs) { statusTime = GetExpiredTime() + expireMs; }
		void SetForced(bool forced1) { forced = forced1; }
		void SetGroupCopy(GuidPosition member) { groupMember = member; }
		// twow-repo#601 S2 / S4 R2 (GrindMode 1): a gap grind trip ends with its quest route backoff - READY, TRAVEL and WORK
		// get the time left until this end (time(nullptr) seconds, 0 = none); its expiry counts as no failure.
		void SetGapWindowEnd(uint32 windowEnd) { gapWindowEnd = windowEnd; }
		uint32 GetGapWindowEnd() const { return gapWindowEnd; }
		// twow-repo#601 S5a (BandLadder): the band trip's wall-clock end (time(nullptr) s), decided once at the step; 0 = no band
		// trip. The trip ends only in CheckStatus (CheckBandTrip / EndBandTrip, QuestBand.cpp) or when another destination
		// replaces it (NoteBandTripReplaced from SetTarget); nothing extends it.
		void SetBandTripEnd(uint32 tripEnd) { bandTripEnd = tripEnd; }
		uint32 GetBandTripEnd() const { return bandTripEnd; }
		void EndBandTrip(uint8 how);            // a TripVerdict as uint8 or ai::quest_band::EndArrived (QuestBand.cpp)
		void NoteBandTripReplaced();            // SetTarget to another destination while the trip ran (QuestBand.cpp)
		uint8 CheckBandTrip() const;            // ai::quest_band::TripVerdict as uint8 (QuestBand.cpp)
		// twow-repo#601 S5a F4 (BandWalkAfterEmptySearches): a band trip that is a backoff walk - its start (0 = a step trip or
		// no band trip) and the zone it started in; set with the trip end by the walk, cleared with it.
		void SetBandWalk(uint32 startedAt, uint32 fromZone) { bandWalkAt = startedAt; bandWalkFromZone = fromZone; }
		uint32 GetBandWalkAt() const { return bandWalkAt; }
		// twow-repo#601 S2 R3 (GrindMode 1, review C-F2): this choice is the gap grind choice (set by ChooseTravelTargetAction on the
		// new target only, never copied) - the R3 window snapshot and the own-map skip apply to it, not to a quest choice.
		void SetGapGrindChoice(bool gapGrindChoice1) { gapGrindChoice = gapGrindChoice1; }
		bool IsGapGrindChoice() const { return gapGrindChoice; }

		void IncRetry(bool isMove) { if (isMove) moveRetryCount+=2; else extendRetryCount++; }
        void DecRetry(bool isMove);

        // twow-repo#485: false when the choice is the taker of a parked turn-in; the target then expires.
        bool CopyTarget(TravelTarget* const target);
        bool IsProgressAwareTurnIn() const;
        // #329: completed turn-ins and incomplete roster quest objectives.
        bool IsProgressAwareQuestTravel() const;
        bool IsTurnInRouteSuppressed(TravelDestination const* destination, WorldPosition const* position) const;
        // twow-repo#601 turn_in hold (ProfessionTraining.TurnInGateSkipSuppressed): the turn-in route of this finished quest is stall- or
        // death-suppressed now (turnin_recovery::IsSuppressedForQuest).
        bool IsTurnInRouteSuppressedForQuest(uint32 questId) const;
        // twow-repo#601 no-route gate (ProfessionTraining.TurnInGateSkipNoRoute): the result of a turn-in route request for its offered
        // finished quests - marked when it found no taker route with every taker judged, unmarked when it found a taker; anything
        // else leaves the mark as it is. Only with the switch on for this bot; one [ProfTravel] state=turnin_no_route line per request
        // without a taker.
        void NoteTurnInRouteResult(std::vector<uint32> const& questIds, bool noRoute, bool takerFound);
        bool IsTurnInNoRoute(uint32 questId) const;
        // twow-repo#485: a finished quest whose turn-in failed TurnInParkFailures times within
        // TurnInParkWindowSeconds is parked for TurnInParkSeconds (roster bots on their own).
        // True when this failure parked the quest.
        bool NoteTurnInFailure(uint32 questId, char const* reason);
        bool IsTurnInParked(uint32 questId) const;
        uint32 ParkedTurnInCount() const;
        // The bot can hand the quest in now and has not parked it (what the taker fetch offers).
        bool IsTurnInOpen(uint32 questId) const;
        // twow-repo#601 S1 (FailureClasses): a structural failure parks the turn-in at once for TurnInParkSeconds
        // (roster bots on their own, parking on - as NoteTurnInFailure). True when this parked it; abandonDue = the
        // second abandonable structural park in a row (ai::turnin_park::ParkNow).
        bool ParkTurnInNow(uint32 questId, char const* reason, bool abandonable, bool& abandonDue);
        // twow-repo#601 S1 (AbandonStructuralParked): a quest abandoned after two structural parks is marked; the
        // giver trip (QuestRelationTravelDestination::IsActive) and the accept (QuestAction::AcceptQuest) skip it.
        void NoteStructuralAbandon(uint32 questId) { turnin_park::MarkAbandoned(abandonedTurnIns, questId); }
        bool IsStructurallyAbandoned(uint32 questId) const { return turnin_park::IsAbandonMarked(abandonedTurnIns, questId); }
        // The move retries ran out; the target goes into cooldown.
        void OnMoveRetryCooldown();
        // twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): an applied rest episode of this bot ended after durMs - the next turn-in
        // observation is a pause, and a running WORK / TRAVEL status clock gets the rest time back (OB-10 condition 1).
        void OnRestEnded(uint32 durMs);
        turnin_recovery::RecoveryAction ObserveTurnInProgress();
        // Counts a death against the current completed-quest turn-in route and
        // puts the route on the death cooldown after too many (#307).
        void OnDeathOnTurnInRoute();
        // #307: counts a death against any current destination (fishing,
        // gathering, grind, quest objective, ...) and suppresses it for this
        // bot after too many.
        void OnDeathAtDestination();
        // #307: only a target the bot is still travelling to or working at.
        bool IsActiveForDeathAttribution() const;
        bool IsDestinationDeathSuppressed(TravelDestination const* destination) const;
        // Re-deaths at camps (AiPlayerbot.DestinationDeaths.CampBlock, CampBlockPolicy.h): records the death position of a roster
        // bot with enough living attackers; a destination point near one is blocked for this bot for CampBlock.Seconds.
        void OnCampDeath(uint32 attackers);
        bool IsCampBlocked(WorldPosition const* position) const;
        // twow-repo#581 (QL-1): a quest objective whose quest this bot failed at too often lately.
        bool IsQuestLoopSuppressed(TravelDestination const* destination) const;
        // twow-repo#601 (QuestLoop.InactiveReselect): an objective whose camp this bot found empty lately (skipped by the search
        // and the refresh), and the way back to the old COOLDOWN when the search after it found no quest target.
        bool IsInactiveReselectMarked(TravelDestination const* destination) const;
        bool TakeInactiveReselectPending() { bool const pending = inactiveReselectPending; inactiveReselectPending = false; return pending; }
        bool IsInactiveReselectPending() const { return inactiveReselectPending; }   // twow-repo#601 IdleDetail (read only)
        bool FallBackToInactiveCooldown();
        // #307 zone escape: the destination that led into a zone clearly above
        // the bot's level is skipped like a death-suppressed one.
        void SuppressCurrentDestination(uint32 durationMs);
        // twow-repo#601 F5 (diagnostic): names the source of the suppression just set (destination_death::Source), and reads it back for
        // the death_suppressed subsets of [QuestFirstRoute] state=rejected. A suppression without a tag counts as a death.
        void TagCurrentSuppression(uint8 source);
        uint8 DestinationSuppressSource(TravelDestination const* destination) const;
        // twow-repo#594 (AiPlayerbot.Path.PosStuck): the travel move just failed through PartialNoop (MoveToTravelTargetAction) -
        // that rule owns the case, the position guard restarts its window.
        void NotePartialNoopHit() { posStuck.Reset(); }
	private:
        // #329: one visible [QuestCommit] line per commitment change of a
        // roster bot's quest target, throttled per event, reason and target.
        void TraceQuestCommit(TravelDestination const* destination, char const* event, char const* reason);
        // twow-repo#601 turn_in hold (QuestLoop.RelationArriveRpgTarget): on arrival at an open giver / taker its nearest spawn
        // becomes the rpg target with the hand-in / accept as the next rpg action.
        void SetRelationRpgTarget();

		// twow-repo#295: with riding stages taken at the unmounted run speed (RidingStagesBotPolicy.h).
		uint32 GetMaxTravelTime() const;

		TravelStatus m_status = TravelStatus::TRAVEL_STATUS_NONE;

		uint32 startTime = WorldTimer::getMSTime();
		uint32 statusTime = 0;
		uint32 gapWindowEnd = 0;   // twow-repo#601 S2 / S4 R2: 0 for every target but a GrindMode 1 gap grind trip
		uint32 bandTripEnd = 0;    // twow-repo#601 S5a: 0 for every target but a running band trip
		uint32 bandWalkAt = 0;     // twow-repo#601 S5a F4: 0 for every target but a running backoff walk
		uint32 bandWalkFromZone = 0;
		bool gapGrindChoice = false;   // twow-repo#601 S2 R3: only the new target of a GrindMode 1 gap grind choice
		bool restPausePending = false;   // twow-repo#602 X1: a rest ended since the last turn-in observation

		bool forced = false;
		bool visitor = true;

		uint32 extendRetryCount = 0;
		uint32 moveRetryCount = 0;

		TravelDestination* tDestination = nullptr;
		std::vector<std::string> travelConditions = {};
		WorldPosition* wPosition = nullptr;
		GuidPosition groupMember;
		uint32 relevance = 0;
        turnin_recovery::State turnInRecovery;
        transport_stall::State transportStall;  // hotfix 8.13 (twow-repo#497)
        struct CommitTrace { uint32 nextAt = 0; uint32 repeats = 0; };
        std::map<std::string, CommitTrace> commitTrace;
        std::map<TravelDestination const*, destination_death::Record> destinationDeaths;
        camp_block::Ring campDeaths;   // re-deaths at camps: the last CampBlockPolicy.h MaxDeaths death positions (bounded)
        // twow-repo#601 (QuestLoop.InactiveAfterProgress): the objective's count when the target was set, -1 = none.
        int32 objectiveCountAtStart = -1;
        // #405: quest objectives whose work phase ran out without progress
        // (same counting rule as the destination deaths, own map).
        std::map<TravelDestination const*, destination_death::Record> workTimeouts;
        // twow-repo#581 (QL-1): objective failures (inactive / work timeout) per quest of this bot, bounded.
        std::map<uint32, quest_loop::Record> questLoops;
        void NoteQuestObjectiveFailure();
        // twow-repo#601 (QuestLoop.InactiveReselect): emptied camps of this bot (fixed ring), and whether the current target ended
        // through one (cleared by the next destination).
        quest_loop::EmptyCamps emptyCamps;
        bool inactiveReselectPending = false;
        // twow-repo#601 F5b (AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait): a work timeout at a contested objective is held back
        // briefly instead of counting toward QuestWorkTimeouts / QuestLoop. True when it handled the timeout.
        bool OnContestedObjectiveTimeout();
        // twow-repo#485: parked turn-ins of this bot (at most 16 quests, in memory only).
        turnin_park::Book turnInParks;
        // twow-repo#601 no-route gate: finished quests whose last turn-in route request found no taker route (at most 32, in memory only).
        std::set<uint32> noRouteTurnIns;
        // twow-repo#601 S1: quests abandoned after two structural parks (at most 8, in memory only).
        turnin_park::AbandonMarks abandonedTurnIns;
        // twow-repo#601 S3c (ZoneExit.TurnInTravelStall): quests of the last turn-in work timeouts (at most 8, in memory only).
        turnin_recovery::WorkTimeoutRing workTimeoutTurnIns;
        void OnWorkTimeout();
        // twow-repo#601 S3c (ZoneExit.TurnInTravelStall): a stage-1 or stage-2 stall inside the near zone of the taker
        // parks the turn-in (structural_unreachable_near), logs [UnreachableTarget] reason=near_stall and leaves the
        // target (not parked: the route suppression and cooldown).
        void OnNearTurnInStall(uint32 questId);
        // S3c: switch on, a finished quest's turn-in, the bot inside TurnInNearRadius of the taker on its map.
        bool IsNearTurnInStall();
        // twow-repo#594 (AiPlayerbot.Path.PosStuck): the position guard's window for the current travel target and the bot's
        // episode series (in memory only, one each per bot).
        pos_stuck::State posStuck;
        pos_stuck::Episodes posStuckEpisodes;
        // Observes the travel target in TRAVEL; true when an episode ended it (the caller returns).
        bool ObservePosStuck();
	};

	//General container for all travel destinations.
	class TravelMgr
	{
	public:
		TravelMgr() {};
        ~TravelMgr() { Clear(); };
		void LoadQuestTravelTable();

		void GetPopulatedGrids();
		void GetPopulatedGrids(uint32 mapId);

		uint32 GetFishZone(const AsyncGuidPosition& pos) const;

		void LoadFishLocations();
		void GetFishLocations();
		void GetFishLocations(uint32 mapId, bool routeAllowed);  // twow-repo#592: routeAllowed = unitless generation gate open
		void SaveFishLocations();
		// twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.Station / Cooking / Fishing / Dungeon / Raid, default 0): the direct-target
		// destinations, built once at boot (DirectTargetLoader.cpp); nothing with every class switch 0.
		void LoadDirectTargets(EntryGuidps const& guidpMap);

		WorldPosition* GetFishSpot(WorldPosition start, bool onlyNearestGrid = false);

		EntryDestinationMap GetExploreLocs() const { return destinationMap.at(TravelDestinationPurpose::Explore); };
		void SetMobAvoidArea();

		DestinationList GetDestinations(const PlayerTravelInfo& info, uint32 purposeFlag = (uint32)TravelDestinationPurpose::None, const std::vector<int32>& entries = {}, bool onlyPossible = true, float maxDistance = 10000.0f) const;
		void GetPartitionsLock(bool getLock = true);
		static bool IsLocationLevelValid(const WorldPosition& position, const PlayerTravelInfo& info, uint32 purposeFlag = (uint32)TravelDestinationPurpose::None, bool gapWindowApplies = true);   // twow-repo#601 S2 R3: false = no GrindMode 1 window (fetches without IsPossible)
		PartitionedTravelList GetPartitions(const WorldPosition& center, const std::vector<uint32>& distancePartitions, const PlayerTravelInfo& info, uint32 purposeFlag = (uint32)TravelDestinationPurpose::None, const std::vector<int32>& entries = {}, bool onlyPossible = true, float maxDistance = 10000.0f) const;
		static void ShuffleTravelPoints(std::vector<TravelPoint>& points);

		void SetNullTravelTarget(TravelTarget* target) const;

		void LoadMapTransfers();
		std::vector<std::pair<WorldPosition, float>> sqMapTransDistances(const WorldPosition& start, uint32 endMapId) const;
		float MapTransDistance(const WorldPosition& start, const WorldPosition& end, bool toMap = false) const;

		void AddBadMmap(uint32 mapId, int x, int y) { badMmap.push_back(std::make_tuple(mapId, x, y)); }
		bool IsBadMmap(uint32 mapId, int x, int y) const { return std::find(badMmap.begin(), badMmap.end(), std::make_tuple(mapId, x, y)) != badMmap.end(); }

		int32 GetAreaLevel(uint32 area_id);
		void LoadAreaLevels();
		// #416 (7.3): creature levels per area in one pass, only while LoadAreaLevels runs.
		void LoadCreatureAreaLevels();
	private:
		void Clear();
		void SetMobAvoidAreaMap(uint32 mapId);

		void SetNullTravelTarget(Player* player) const;
		void AddMapTransfer(WorldPosition start, WorldPosition end, float portalDistance = 0.1f, bool makeShortcuts = true);

		template<class T>
		T* AddDestination(const int32 entry, const TravelDestinationPurpose purpose, const uint32 questId = 0) {
			uint32 id = questId ? questId : entry;

			if (questId)
			{
				if (destinationMap[purpose].find(questId) != destinationMap[purpose].end())
					for(auto& dest : destinationMap[purpose][questId])
						if(dest->GetEntry() == entry)
							return (T*)dest;
			}
			else
			{
				if (destinationMap[purpose].find(entry) != destinationMap[purpose].end())
					for (auto& dest : destinationMap[purpose][entry])
						return (T*)destinationMap[purpose][entry].front();
			}

			destinationMap[purpose][id].push_back(new T(purpose, questId, entry));

			return (T*)destinationMap[purpose][id].back();
		}

		bool populatedGrids[1024][64][64] = { false };

		NullTravelDestination* nullTravelDestination = new NullTravelDestination();
		WorldPosition* nullWorldPosition = new WorldPosition();
		PurposeDestinationMap destinationMap;

		std::unordered_map<uint64, AsyncGuidPosition> pointsMap;

		GatherTravelDestination fishMap;
		std::list<AsyncGuidPosition> fishPoints;
		// twow-repo#594 / P3-b: the snapped points of the direct-target destinations (never in pointsMap).
		std::list<AsyncGuidPosition> directPoints;
		std::unordered_map<uint32, int32> areaLevels;
		// #416 (7.3): areaLevels is read from the map threads and the destination jobs.
		std::recursive_mutex areaLevelMutex;
		ai::area_level::CreatureLevels creatureAreaLevels;
		bool loadingAreaLevels = false;

		std::mutex getDestinationMutex;
		std::condition_variable getDestinationVar;
		uint8 availableDestinationWorkers = 5;

		std::vector<std::tuple<uint32, int, int>> badMmap;

		std::unordered_map<std::pair<uint32, uint32>, std::vector<MapTransfer>, boost::hash<std::pair<uint32, uint32>>> mapTransfersMap;
	};
}

#define sTravelMgr MaNGOS::Singleton<TravelMgr>::Instance()

