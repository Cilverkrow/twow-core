#include "playerbot/playerbot.h"
#include "TankRogueStrategy.h"

using namespace ai;

void TankRogueStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Taunt (design D-2 "Spit", 15 yd, main target plus two around it).
    triggers.push_back(new TriggerNode(
        "lose aggro",
        NextAction::array(0, new NextAction("rogue taunt", ACTION_HIGH + 5), NULL)));

    triggers.push_back(new TriggerNode(
        "medium health",
        NextAction::array(0, new NextAction("evasion", ACTION_HIGH + 3), NULL)));

    // Owner 2026-09-27: existing abilities first - Riposte after a parry,
    // Ghostly Strike uptime for dodge. The Turtle parry finisher follows once
    // OB-20 has identified the spell.
    triggers.push_back(new TriggerNode(
        "riposte",
        NextAction::array(0, new NextAction("riposte", ACTION_HIGH + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "ghostly strike",
        NextAction::array(0, new NextAction("ghostly strike", ACTION_HIGH + 2), NULL)));
}

void TankRogueStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TankRogueThreatMultiplier(ai));
}

float TankRogueThreatMultiplier::GetValue(Action* action)
{
    std::string const name = action->getName();
    if (name == "feint" || name == "vanish")
        return 0.0f;

    return 1.0f;
}
