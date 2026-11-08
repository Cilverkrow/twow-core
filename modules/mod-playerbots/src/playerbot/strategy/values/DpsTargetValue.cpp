
#include "playerbot/playerbot.h"
#include "playerbot/CombatRule.h"   // A3 step 5 P5: TargetValueTimer (CombatRule.TargetRule, log only)
#include "DpsTargetValue.h"
#include "LeastHpTargetValue.h"

using namespace ai;


Unit* DpsTargetValue::Calculate()
{
    ai::combat_rule::TargetValueTimer const costTimer(ai, ai::combat_rule::TargetValue::DpsTarget);   // A3 step 5 P5: TargetValues line (log only)
    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindLeastHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}

class FindMaxHpTargetStrategy : public FindTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* ai) : FindTargetStrategy(ai)
    {
        maxHealth = 0;
    }

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
    {
        Group* group = ai->GetBot()->GetGroup();
        if (group)
        {
            uint64 guid = group->GetTargetIcon(4);
            if (guid && attacker->GetObjectGuid() == ObjectGuid(guid))
                return;
        }
        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    Unit* rti = RtiTargetValue::Calculate();
    if (rti) return rti;

    FindMaxHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}
