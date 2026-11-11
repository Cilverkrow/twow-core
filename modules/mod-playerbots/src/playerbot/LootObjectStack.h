#pragma once
#include "playerbot.h"
#include "playerbot/GatheringPacePolicy.h"   // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach)
#include "playerbot/LootGiveUpPolicy.h"   // twow-repo#703 item 3 (AiPlayerbot.Loot.MoveGiveUp)

class PlayerbotAI;

namespace ai
{
    class ItemQualifier;

    class LootObject
    {
    public:
        LootObject() : skillId(0), reqSkillValue(0), reqItem(0) {}
        LootObject(Player* bot, ObjectGuid guid);
        LootObject(const LootObject& other);

    public:
        bool IsEmpty() { return !guid; }
        bool IsLootPossible(Player* bot);
        void Refresh(Player* bot, ObjectGuid guid, bool debug = false);
        WorldObject* GetWorldObject(Player* bot);
        ObjectGuid guid;

        uint32 skillId;
        uint32 reqSkillValue;
        uint32 reqItem;
    };

    class LootTarget
    {
    public:
        LootTarget(ObjectGuid guid);
        LootTarget(LootTarget const& other);

    public:
        LootTarget& operator=(LootTarget const& other);
        bool operator< (const LootTarget& other) const;

    public:
        ObjectGuid guid;
        time_t asOfTime;
    };

    class LootTargetList : public std::set<LootTarget>
    {
    public:
        void shrink(time_t fromTime);
    };

    class LootObjectStack
    {
    public:
        LootObjectStack(Player* bot) : bot(bot) {}
        ~LootObjectStack();

    public:
        bool Add(ObjectGuid guid);
        void Remove(ObjectGuid guid);
        void Clear();
        bool CanLoot(float maxDistance);
        LootObject GetLoot(float maxDistance = 0);

    public:
        std::vector<LootObject> OrderByDistance(float maxDistance = 0);
        // twow-repo#333 T28b: the far-pick state of this bot (GatheringPacePolicy.h); read and written on the bot's own thread.
        gathering_pace::State& Pace() { return pace; }
        // twow-repo#703 item 3 (AiPlayerbot.Loot.MoveGiveUp, LootGiveUpPolicy.h): the approach state of "move to loot" and the
        // give-up. GiveUp is called only by MoveToLootAction (the key on): the object leaves the list and Add refuses it for
        // loot_giveup::ExcludeSeconds; Add is the only insert, so no loot list reader (has available loot, LootAction, the
        // travel / rpg pauses) offers it. Both read and written on the bot's own thread.
        loot_giveup::Try& MoveTry() { return moveTry; }
        void GiveUp(ObjectGuid guid, int64 now);

    private:
        bool PaceFarOk(PlayerbotAI* ai, LootObject& lootObject, uint32 now);

        Player* bot;
        LootTargetList availableLoot;
        gathering_pace::State pace;
        loot_giveup::BlockList giveUp;   // twow-repo#703 item 3: filled only by GiveUp, so empty with the key 0
        loot_giveup::Try moveTry;
    };

    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach, GatheringPacePolicy.h). The callers test the key first.
    // The bot's AI when the lever applies to it (its cohort, not grouped, a roster bot on its own), else nullptr.
    PlayerbotAI* GatheringPaceScope(Player* bot);
    // LootAction set the loot target: a far target starts its pick clock, a near one lets a pending far pick go.
    void GatheringPacePick(PlayerbotAI* ai, LootObject& lootObject);
    // MoveToLootAction / OpenLootAction: the pending far pick TimeoutSeconds after its pick without an open, or with a strong
    // hostile cached near its node, while the bot is beyond the loot distance - resets "loot target" and returns true.
    bool GatheringPaceTimeout(PlayerbotAI* ai, LootObject const& lootObject);
    // OpenLootAction: a cast started on the pending far pick ends its clock.
    void GatheringPaceOpened(PlayerbotAI* ai, LootObject const& lootObject);

};
