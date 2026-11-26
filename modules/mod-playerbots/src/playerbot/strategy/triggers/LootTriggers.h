#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/GroupQuestLootPolicy.h"   // twow-repo#780: Loot.GroupQuestLoot

namespace ai
{
    // twow-repo#780 (AiPlayerbot.Loot.GroupQuestLoot, GroupQuestLootPolicy.h): the key is on, the bot is a non-leader group member
    // with an active player master, the creature corpse holds the bot's own party-loot quest drop, and the bot's leash hold of
    // that corpse has not expired.
    bool GroupQuestLootCorpse(PlayerbotAI* ai, Player* bot, WorldObject* wo);
    // twow-repo#780 (d): the follow leash ("out of free move range") stays quiet while the loot target is such a quest corpse,
    // the bot is not in combat, the bound holds, and for at most LeashHoldMaxSeconds per corpse. The hold lives in the bot's
    // LootObjectStack (GroupQuestHold).
    bool GroupQuestLootLeashQuiet(PlayerbotAI* ai, Player* bot);

    class LootAvailableTrigger : public Trigger
    {
    public:
        LootAvailableTrigger(PlayerbotAI* ai) : Trigger(ai, "loot available") {}

        virtual bool IsActive() override;
    };

    class FarFromCurrentLootTrigger : public Trigger
    {
    public:
        FarFromCurrentLootTrigger(PlayerbotAI* ai) : Trigger(ai, "far from current loot") {}

        virtual bool IsActive() override;
    };

    class CanLootTrigger : public Trigger
    {
    public:
        CanLootTrigger(PlayerbotAI* ai) : Trigger(ai, "can loot") {}

        virtual bool IsActive() override;
    };

    // twow-repo#568 (D48): the "very often" cadence (checked every 5 s) for the
    // roll strategy, but only active while the bot is in a group and has an open roll, the first two conditions
    // of AutoLootRollAction::isPossible. Used by RollStrategy.
    class LootRollPendingTrigger : public Trigger
    {
    public:
        LootRollPendingTrigger(PlayerbotAI* ai) : Trigger(ai, "loot roll pending", 5) {}

        virtual bool IsActive() override;
    };
}
