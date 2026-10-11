
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "DruidTriggers.h"
#include "DruidActions.h"

using namespace ai;

bool NaturesGraspReadyTrigger::IsActive()
{
    uint32 const graspId = AI_VALUE2(uint32, "spell id", "nature's grasp");
    if (!graspId)
        return false;

    SpellEntry const* const graspInfo = sServerFacade.LookupSpellInfo(graspId);
    if (bot->HasSpellCooldown(graspId) || (graspInfo && bot->HasSpellCategoryCooldown(graspInfo->Category)))
        return false;

    return EnemyInRangeTrigger::IsActive();
}

bool EntanglingRootsKiteTrigger::IsActive() 
{ 
	if (!DebuffTrigger::IsActive())
		return false;

    if (AI_VALUE(uint8, "attackers count") > 3)
        return false;

	if (!GetTarget()->HasMana())
		return false;

    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;

        if (ai->HasMyAura("entangling roots", unit))
            return false;
    }

    return !HasMaxDebuffs();
}

Value<Unit*>* LifebloomTankTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("party tank without lifebloom", "lifebloom");
}