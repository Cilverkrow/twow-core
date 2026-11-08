#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    // A3 step 5 (ADR-0041; AiPlayerbot.CombatRule.RestBeforePull, default 0): the combat rule's own non-combat node. AiFactory adds
    // it only to the bots the part applies to, so with the part off no engine holds it. The rest triggers fire while the rule says
    // the bot has to rest before its next pull (ai::combat_rule::RestBeforePullNeed) and run food / drink at 7.0 - above choose
    // travel target (6.98) and attack anything (5), with no lower HP bound.
    class CombatRuleStrategy : public Strategy
    {
    public:
        CombatRuleStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "combat rule"; }

    private:
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    };

    class CombatRuleRestHealthTrigger : public Trigger
    {
    public:
        CombatRuleRestHealthTrigger(PlayerbotAI* ai) : Trigger(ai, "combat rule rest health") {}

        bool IsActive() override;
    };

    class CombatRuleRestManaTrigger : public Trigger
    {
    public:
        CombatRuleRestManaTrigger(PlayerbotAI* ai) : Trigger(ai, "combat rule rest mana") {}

        bool IsActive() override;
    };
}
