
#include "playerbot/playerbot.h"
#include "RangedCombatStrategy.h"
#include "playerbot/OomBackoffPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

void RangedCombatStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "enemy too close for spell",
        NextAction::array(0, new NextAction("flee", ACTION_MOVE), NULL)));

    // twow-repo#602 ROT-01 3c-B (AiPlayerbot.Rotation.OomBackoff): a priest, mage or warlock out of mana without a wand walks
    // into melee and attacks (shoot is pushed at ACTION_NORMAL by "no mana" and is impossible without a wand). Off = no node.
    if (ai::oom_backoff::Applies(sPlayerbotAIConfig.rotationOomBackoff, ai->GetBot()->GetGUIDLow()))
        triggers.push_back(new TriggerNode(
            "oom melee fallback",
            NextAction::array(0, new NextAction("reach melee", ACTION_NORMAL + 1), new NextAction("melee", ACTION_NORMAL), NULL)));
}
