#include "playerbot/playerbot.h"
#include "CombatRuleStrategy.h"
#include "playerbot/CombatRule.h"

using namespace ai;

void CombatRuleStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "combat rule rest health",
        NextAction::array(0, new NextAction("food", 7.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "combat rule rest mana",
        NextAction::array(0, new NextAction("drink", 7.0f), NULL)));
}

bool CombatRuleRestHealthTrigger::IsActive()
{
    if (ai::combat_rule::RestBeforePullNeed(ai) != ai::combat_rule::RestNeed::Health)
        return false;
    ai::combat_rule::CountRest(ai, ai::combat_rule::RestNeed::Health);
    return true;
}

bool CombatRuleRestManaTrigger::IsActive()
{
    if (ai::combat_rule::RestBeforePullNeed(ai) != ai::combat_rule::RestNeed::Mana)
        return false;
    ai::combat_rule::CountRest(ai, ai::combat_rule::RestNeed::Mana);
    return true;
}
