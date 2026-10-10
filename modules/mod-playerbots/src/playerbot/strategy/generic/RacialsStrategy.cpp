
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "RacialsStrategy.h"

using namespace ai;

void RacialsStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // twow-repo#541 (audit A13): gift of the naaru, mana tap and arcane torrent have no action creator in
    // this build (ActionContext.h, #ifndef MANGOSBOT_ZERO), so their queue entries can only end UNKNOWN.
    // AiPlayerbot.Perf.RacialsSkipUnbuildable = 1 does not register these three nodes (0 = all twelve
    // nodes as before). NOT strictly behaviour-neutral: today a lingering 71 entry can make the engine
    // re-push another action without prerequisites; with 1 that action takes its normal prerequisite path.
#ifdef MANGOSBOT_ZERO
    const bool skipUnbuildable = sPlayerbotAIConfig.perfRacialsSkipUnbuildable;
#else
    const bool skipUnbuildable = false;
#endif

    if (!skipUnbuildable)
    {
        triggers.push_back(new TriggerNode(
            "low health",
            NextAction::array(0, new NextAction("gift of the naaru", 71.0f), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "melee medium aoe",
        NextAction::array(0, new NextAction("war stomp", 71.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "war stomp",
        NextAction::array(0, new NextAction("war stomp", 71.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "cannibalize",
        NextAction::array(0, new NextAction("cannibalize", 71.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "perception",
        NextAction::array(0, new NextAction("perception", 71.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "rooted",
        NextAction::array(0, new NextAction("escape artist", 71.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "will of the forsaken",
        NextAction::array(0, new NextAction("will of the forsaken", 71.0f), NULL)));

    /*triggers.push_back(new TriggerNode(
        "shadowmeld",
        NextAction::array(0, new NextAction("shadowmeld", 71.0f), NULL)));*/

    triggers.push_back(new TriggerNode(
        "berserking",
        NextAction::array(0, new NextAction("berserking", 58.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "blood fury",
        NextAction::array(0, new NextAction("blood fury", 71.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "stoneform",
        NextAction::array(0, new NextAction("stoneform", 71.0f), NULL)));

    if (!skipUnbuildable)
    {
        triggers.push_back(new TriggerNode(
            "mana tap",
            NextAction::array(0, new NextAction("mana tap", 71.0f), NULL)));

        triggers.push_back(new TriggerNode(
            "arcane torrent",
            NextAction::array(0, new NextAction("arcane torrent", 71.0f), NULL)));
    }
}

void RacialsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitNonCombatTriggers(triggers);
}
