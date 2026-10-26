#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/AiObjectContext.h"

namespace ai
{
    //Definition
    using DestinationEntry = int32;
    using EntryGuidps = std::unordered_map<DestinationEntry, std::vector<AsyncGuidPosition>>;

    //All creature and gameobject entries and reason a bot might want to travel to them.
    class EntryGuidpsValue : public SingleCalculatedValue<EntryGuidps>
    {
    public:
        EntryGuidpsValue(PlayerbotAI* ai, std::string name = "entry guidps") : SingleCalculatedValue(ai, name) {};

        virtual EntryGuidps Calculate() override;
    };

    using DestinationPurose = uint32;

    using EntryTravelPurposeMap = std::unordered_map<DestinationEntry, DestinationPurose>;

    //All creature and gameobject entries and reason a bot might want to travel to them.
    class EntryTravelPurposeMapValue : public SingleCalculatedValue<EntryTravelPurposeMap>
    {
    public:
        EntryTravelPurposeMapValue(PlayerbotAI* ai, std::string name = "entry travel purpose") : SingleCalculatedValue(ai, name) {};

        virtual EntryTravelPurposeMap Calculate() override;

        static uint32 SkillIdToGatherEntry(int32 entry);
    };

    enum class TravelDestinationPurpose : uint32
    {
        None = 0,
        QuestGiver = 1 << 0,
        QuestObjective1 = 1 << 1,
        QuestObjective2 = 1 << 2,
        QuestObjective3 = 1 << 3,
        QuestObjective4 = 1 << 4,
        QuestAllObjective = QuestObjective1 | QuestObjective2 | QuestObjective3 | QuestObjective4,
        QuestTaker = 1 << 5,
        GenericRpg = 1 << 6,
        Trainer = 1 << 7,
        Repair = 1 << 8,
        Vendor = 1 << 9,
        AH = 1 << 10,
        Mail = 1 << 11,
        Grind = 1 << 12,
        Boss = 1 << 13,
        GatherSkinning = 1 << 14,
        GatherMining = 1 << 15,
        GatherHerbalism = 1 << 16,
        GatherFishing = 1 << 17,
        Explore = 1 << 18,
        // twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.*, default 0): anvil / forge / cooking fire, dungeon and raid
        // entrances. Cooking is Station with focus 4; fishing shore points are GatherFishing (FishAction acts on it).
        Station = 1 << 19,
        DungeonEntrance = 1 << 20,
        RaidEntrance = 1 << 21,
        MaxFlag = 1 << 22
    };

    const std::unordered_map<TravelDestinationPurpose, std::string> TravelDestinationPurposeName =
    {
        {TravelDestinationPurpose::None, "None"},
        {TravelDestinationPurpose::QuestGiver, "QuestGiver"},
        {TravelDestinationPurpose::QuestObjective1, "QuestObjective1"},
        {TravelDestinationPurpose::QuestObjective2, "QuestObjective2"},
        {TravelDestinationPurpose::QuestObjective3 , "QuestObjective3"},
        {TravelDestinationPurpose::QuestObjective4, "QuestObjective4"},
        {TravelDestinationPurpose::QuestTaker, "QuestTaker"},
        {TravelDestinationPurpose::GenericRpg , "GenericRpg"},
        {TravelDestinationPurpose::Trainer, "Trainer"},
        {TravelDestinationPurpose::Repair, "Repair"},
        {TravelDestinationPurpose::Vendor, "Vendor"},
        {TravelDestinationPurpose::AH , "AH"},
        {TravelDestinationPurpose::Mail , "Mail"},
        {TravelDestinationPurpose::Grind , "Grind"},
        {TravelDestinationPurpose::Boss, "Boss"},
        {TravelDestinationPurpose::GatherSkinning , "GatherSkinning"},
        {TravelDestinationPurpose::GatherMining, "GatherMining"},
        {TravelDestinationPurpose::GatherHerbalism, "GatherHerbalism"},
        {TravelDestinationPurpose::GatherFishing, "GatherFishing"},
        {TravelDestinationPurpose::Explore, "Explore"},
        {TravelDestinationPurpose::Station, "Station"},
        {TravelDestinationPurpose::DungeonEntrance, "DungeonEntrance"},
        {TravelDestinationPurpose::RaidEntrance, "RaidEntrance"},
        {TravelDestinationPurpose::MaxFlag, "MaxFlag"}
    };

    class TravelDestination;

    using DestinationList = std::vector<TravelDestination*>;

    //TypedDestinationMap[Purpose][QuestId/Entry]={TravelDestination}
    using EntryDestinationMap = std::unordered_map<int32, DestinationList>;
    using PurposeDestinationMap = std::unordered_map<TravelDestinationPurpose, EntryDestinationMap>;

    //Usage

    //TravelPoint[point, destination, distance]
    using TravelPoint = std::tuple<TravelDestination*, WorldPosition*, float>;
    using TravelPointList = std::vector<TravelPoint>;
    using PartitionedTravelList = std::map<uint32, TravelPointList>;
    // #416: a std::future from std::async blocks in its destructor until the
    // job is done. Replacing or clearing a pending list on the map thread
    // therefore waited for the whole search; this wrapper parks a running job
    // instead (see TravelChoosePolicy.h).
    class FutureDestinations
    {
    public:
        FutureDestinations() = default;
        FutureDestinations(std::future<PartitionedTravelList>&& job) : future(std::move(job)) {}
        FutureDestinations(FutureDestinations const&) = delete;
        FutureDestinations& operator=(FutureDestinations const&) = delete;
        FutureDestinations& operator=(FutureDestinations&& other) { if (this != &other) { Park(); future = std::move(other.future); } return *this; }
        FutureDestinations& operator=(std::future<PartitionedTravelList>&& job) { Park(); future = std::move(job); return *this; }
        ~FutureDestinations() { Park(); }

        bool valid() const { return future.valid(); }
        template <class Rep, class Period>
        std::future_status wait_for(std::chrono::duration<Rep, Period> const& timeout) const { return future.wait_for(timeout); }
        PartitionedTravelList get() { return future.get(); }

        // #416: false while the parking lot is full - the request is retried
        // next update instead of starting one more job.
        static bool MayStart();
        // #416 (7.3): parked jobs, for [MemStores].
        static size_t ParkedCount();
        // #416: drops finished parked jobs (from the bot updates) and writes the
        // [TravelChoose] state=summary line.
        static void Collect();

    private:
        void Park();

        std::future<PartitionedTravelList> future;
    };

    typedef std::set<uint32> focusQuestTravelList;

    class FocusTravelTargetValue : public ManualSetValue<focusQuestTravelList>
    {
    public:
        FocusTravelTargetValue(PlayerbotAI* ai, focusQuestTravelList defaultValue = {}, std::string name = "focus travel target") : ManualSetValue<focusQuestTravelList>(ai, defaultValue, name) {};
    };

    class HasFocusTravelTargetValue : public BoolCalculatedValue
    {
    public:
        HasFocusTravelTargetValue(PlayerbotAI* ai, std::string name = "has focus travel target", int checkInterval = 10) : BoolCalculatedValue(ai, name, checkInterval) {}

        virtual bool Calculate() override { return !AI_VALUE(focusQuestTravelList, "focus travel target").empty(); };
    };

    //Target fetching

    class FutureTravelDestinationsValue : public ManualSetValue<FutureDestinations*>
    {
    public:
        FutureTravelDestinationsValue(PlayerbotAI* ai, std::string name = "future travel destinations") : ManualSetValue<FutureDestinations*>(ai, new FutureDestinations, name) {}

        ~FutureTravelDestinationsValue() override { delete value; }
    };

    class NoActiveTravelDestinationsValue : public ManualSetValue<bool>, public Qualified
    {
    public:
        NoActiveTravelDestinationsValue(PlayerbotAI* ai, std::string name = "no active travel destinations") : ManualSetValue<bool>(ai, {}, name), Qualified() {}
    };

    class TravelTargetActiveValue : public BoolCalculatedValue
    {
    public:
        TravelTargetActiveValue(PlayerbotAI* ai, std::string name = "travel target active", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };

    class TravelTargetReadyValue : public BoolCalculatedValue
    {
    public:
        TravelTargetReadyValue(PlayerbotAI* ai, std::string name = "travel target ready", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };    

    class TravelTargetTravelingValue : public BoolCalculatedValue
    {
    public:
        TravelTargetTravelingValue(PlayerbotAI* ai, std::string name = "travel target traveling", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval){};

        virtual bool Calculate() override;
    };    

    class TravelTargetWorkingValue : public BoolCalculatedValue
    {
    public:
        TravelTargetWorkingValue(PlayerbotAI* ai, std::string name = "travel target working", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };

    class NeedTravelPurposeValue : public BoolCalculatedValue, public Qualified
    {
    public:
        NeedTravelPurposeValue(PlayerbotAI* ai, std::string name = "need travel purpose", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};

        virtual bool Calculate() override;
    };

    class ShouldTravelNamedValue : public BoolCalculatedValue, public Qualified
    {
    public:
        ShouldTravelNamedValue(PlayerbotAI* ai, std::string name = "should travel named", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};

        virtual bool Calculate() override;
    };

    class InOverworldValue : public BoolCalculatedValue
    {
    public:
        InOverworldValue(PlayerbotAI* ai, std::string name = "in overworld", int checkInterval = 10) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override {return WorldPosition(bot).isOverworld();}
    };

    //Keep using this target

    class QuestStageActiveValue : public BoolCalculatedValue, public Qualified
    {
    public:
        QuestStageActiveValue(PlayerbotAI* ai, std::string name = "quest stage active", int checkInterval = 1) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};

        virtual bool Calculate() override;
    };

    // twow-repo#333 Berufe P1-a / P1-b (ProfessionTravelPolicy.h), shared by "should travel named" and the request:
    // the trainers that teach this bot a GREEN rank of a phase-one profession now, and the gathering tool it lacks
    // (0 = none missing).
    // onlyUnlearned (P1-n): only the skills the bot does not have yet (their first rank).
    std::vector<int32> PhaseOneProfessionTrainers(PlayerbotAI* ai, bool onlyUnlearned = false);
    uint32 MissingGatherTool(PlayerbotAI* ai);

    // P1-b2 (CLI-PATH 25.10.): the entries a profession trip may go to - not hostile to the bot, one spawn reachable
    // (ProfessionTravelPolicy.h ClassifyTripTarget); the vendors of a tool. The dropped counts go to the [ProfTravel] line.
    // P1-u: below level 10 also a spawn in the bot's zone (ProfessionTravelPolicy.h HomeZoneOnly).
    struct ProfessionTripDrops
    {
        uint32 hostile = 0;
        uint32 unreachable = 0;
        uint32 homeZone = 0;   // P1-u: friendly and reachable, but no spawn in the bot's zone
    };
    std::vector<int32> ProfessionTripTargets(PlayerbotAI* ai, std::vector<int32> const& entries, TravelDestinationPurpose purpose,
        ProfessionTripDrops* drops = nullptr);
    std::vector<int32> GatherToolVendors(PlayerbotAI* ai, uint32 tool);

    // P1-n (OB-00 26.10., AiPlayerbot.ProfessionTraining.NearRadius): the reachable, not hostile trainers of a phase-one skill
    // the bot has not learned yet with a spawn within the radius (ProfessionTravelPolicy.h NearTripSkill / WithinNearRadius).
    struct NearTrainerCounts
    {
        uint32 trainers = 0;   // trainers of an unlearned phase-one skill, anywhere
        uint32 inRadius = 0;   // of these, with a spawn within the radius on the bot's map
    };
    std::vector<int32> NearProfessionTrainers(PlayerbotAI* ai, uint32 radius, ProfessionTripDrops* drops = nullptr,
        NearTrainerCounts* counts = nullptr);

    // P1-v (M1r run 3: 20 requests, no target): the destination list of a profession trip, built like GetPartitions (closest
    // partition per destination, its nearest point) but without the fight gate of IsLocationLevelValid; homeZone != 0 (P1-u)
    // keeps only the points in that zone. Safe in the async request.
    class PlayerTravelInfo;
    PartitionedTravelList ProfessionTripPartitions(WorldPosition const& center, std::vector<uint32> const& partitions,
        PlayerTravelInfo const& info, TravelDestinationPurpose purpose, std::vector<int32> const& entries, uint32 homeZone);
    // P1-v: a profession trip whose list gave no target - one "[ProfTravel] state=no_target" line with the reject counts, and
    // the open trip ends at once (reason=no_target) instead of an hour later as "lost". The hourly limit stays spent.
    void ProfessionTripNoTarget(PlayerbotAI* ai, std::string const& travelPurpose, uint32 ranges, uint32 points,
        std::string const& rejects);
}