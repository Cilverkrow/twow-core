
#include "playerbot/playerbot.h"
#include "UsePotionsStrategy.h"
#include "playerbot/DarkRuneGatePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

class UsePotionsStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    UsePotionsStrategyActionNodeFactory()
    {
        creators["healthstone"] = &healthstone;
        creators["healing potion"] = &healing_potion;
        creators["dark rune"] = &dark_rune;
    }

private:
    ACTION_NODE_A(healthstone, "healthstone", "healing potion");

    ACTION_NODE_A(healing_potion, "healing potion", "use bandage");

    ACTION_NODE_A(dark_rune, "dark rune", "mana potion");
};

UsePotionsStrategy::UsePotionsStrategy(PlayerbotAI* ai) : Strategy(ai)
{
    actionNodeFactories.Add(std::make_unique<UsePotionsStrategyActionNodeFactory>());
}

void UsePotionsStrategy::InitCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("healthstone", ACTION_CRITICAL_HEAL + 1), NULL)));

#ifdef MANGOSBOT_ZERO
    triggers.push_back(new TriggerNode(
        "critical health",
        NextAction::array(0, new NextAction("whipper root tuber", ACTION_CRITICAL_HEAL), NULL)));
#endif

    triggers.push_back(new TriggerNode(
        "low health",
        NextAction::array(0, new NextAction("use bandage", ACTION_MEDIUM_HEAL), NULL)));

    // twow-repo#602 ROT-01 3c (AiPlayerbot.Rotation.DarkRuneGate): with the switch "dark rune" is pushed only while it or
    // its alternative "mana potion" is usable, and a failed check waits (DarkRuneGatePolicy.h); off = "low mana" as before.
    bool const darkRuneGate = ai::dark_rune_gate::Applies(sPlayerbotAIConfig.rotationDarkRuneGate, ai->GetBot()->GetGUIDLow());
    triggers.push_back(new TriggerNode(
        darkRuneGate ? "low mana consumable" : "low mana",
        NextAction::array(0, new NextAction("dark rune", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "has poison debuff",
        NextAction::array(0, new NextAction("anti-venom", ACTION_DISPEL), NULL)));
}

void UsePotionsStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}