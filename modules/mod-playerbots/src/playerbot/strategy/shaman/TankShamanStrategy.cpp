#include "playerbot/playerbot.h"
#include "TankShamanStrategy.h"
#include "playerbot/TankPathPolicy.h"

using namespace ai;

void TankShamanStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Taunt: Turtle's Earthshaker Slam (51365, needs a shield, 10 s cooldown).
    triggers.push_back(new TriggerNode(
        "lose aggro",
        NextAction::array(0, new NextAction("earthshaker slam", ACTION_HIGH + 5), NULL)));

    // Shield charges are the tank's mitigation (Stable Shields, design #392).
    triggers.push_back(new TriggerNode(
        "lightning shield",
        NextAction::array(0, new NextAction("lightning shield", ACTION_HIGH + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "shaman weapon",
        NextAction::array(0, new NextAction("rockbiter weapon", ACTION_HIGH + 1), NULL)));
}

void TankShamanStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "shaman weapon",
        NextAction::array(0, new NextAction("rockbiter weapon", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "lightning shield",
        NextAction::array(0, new NextAction("lightning shield", ACTION_NORMAL + 1), NULL)));
}

void TankShamanStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TankShamanMultiplier(ai));
}

void TankShamanStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TankShamanMultiplier(ai));
}

float TankShamanMultiplier::GetValue(Action* action)
{
    std::string const name = action->getName();
    if (name == "windfury weapon" || name == "flametongue weapon" || name == "frostbrand weapon")
        return 0.0f;

    if (name == "stormstrike")
    {
        Unit* target = AI_VALUE(Unit*, "current target");
        bool const holdsAggro = target && target->GetVictim() == bot;

        uint32 charges = 0;
        if (Aura* shield = ai->GetAura("lightning shield", bot))
            charges = shield->GetHolder()->GetAuraCharges();

        if (!ai::tank_path::AllowTankStormstrike(holdsAggro, charges))
            return 0.0f;
    }

    return 1.0f;
}
