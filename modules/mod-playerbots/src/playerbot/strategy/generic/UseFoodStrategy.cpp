
#include "playerbot/playerbot.h"
#include "UseFoodStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

void UseFoodStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("food", 6.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "high mana",
        NextAction::array(0, new NextAction("drink", 6.0f), NULL)));

    // twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): eat / drink up to the rest targets while a rest episode runs. Added only
    // with the switch on, so the engine is unchanged at 0 (like GrindMode 2); switching needs a strategy reset or restart.
    if (sPlayerbotAIConfig.rotationRestGate)
    {
        triggers.push_back(new TriggerNode(
            "rest health",
            NextAction::array(0, new NextAction("food", 6.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "rest mana",
            NextAction::array(0, new NextAction("drink", 6.0f), NULL)));
    }
}
