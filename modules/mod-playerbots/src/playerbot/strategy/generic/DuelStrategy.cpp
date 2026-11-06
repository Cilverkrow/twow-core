
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "DuelStrategy.h"

using namespace ai;

void DuelStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    PassTroughStrategy::InitNonCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "duel requested",
        NextAction::array(0, new NextAction("accept duel", relevance), NULL)));

    // twow-repo#568 (D69): read at strategy (engine) build.
    triggers.push_back(new TriggerNode(
        "duel no attackers",
        NextAction::array(0, new NextAction("attack duel opponent", 70.0f), NULL)));

    // twow-repo#602 W24 duel hang (AiPlayerbot.Rotation.DuelRule, DuelRulePolicy.h): a bot-vs-bot duel without a fight for
    // Rotation.DuelRule.CancelSeconds is cancelled; above "select new target" (90), which failed on every tick in the hang.
    triggers.push_back(new TriggerNode(
        "duel stalled",
        NextAction::array(0, new NextAction("cancel duel", 95.0f), NULL)));
}

void DuelStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
}