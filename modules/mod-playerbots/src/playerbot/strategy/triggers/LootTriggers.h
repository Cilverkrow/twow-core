#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/values/LastMovementValue.h"

namespace ai
{
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

    // twow-repo#568 (D48, AiPlayerbot.Diet.LootRollPending): the "very often" cadence (checked every 5 s) for the
    // roll strategy, but only active while the bot is in a group and has an open roll, the first two conditions
    // of AutoLootRollAction::isPossible. Used by RollStrategy only with the switch on.
    class LootRollPendingTrigger : public Trigger
    {
    public:
        LootRollPendingTrigger(PlayerbotAI* ai) : Trigger(ai, "loot roll pending", 5) {}

        virtual bool IsActive() override;
    };
}
