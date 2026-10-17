#pragma once
#include "NonCombatStrategy.h"
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
    // twow-repo#601 GrindMode 2 (AiPlayerbot.QuestFirstProgression.GrindMode, owner 17.10.2026): owned by the "grind"
    // strategy and only added while GrindMode is 2 (restart-only). For a quest-first roster bot on its own it turns the
    // strategy's "attack anything" down unless the target attacks the bot or its group or is needed for an active quest.
    class QuestGrindModeMultiplier : public Multiplier
    {
    public:
        QuestGrindModeMultiplier(PlayerbotAI* ai) : Multiplier(ai, "quest grind mode") {}
        float GetValue(Action* action) override;
    };

    class GrindingStrategy : public NonCombatStrategy
    {
    public:
        GrindingStrategy(PlayerbotAI* ai) : NonCombatStrategy(ai) {}
        std::string getName() override { return "grind"; }
        int GetType() override { return STRATEGY_TYPE_DPS; }

    private:
        void InitNonCombatTriggers(std::list<TriggerNode*> &triggers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
