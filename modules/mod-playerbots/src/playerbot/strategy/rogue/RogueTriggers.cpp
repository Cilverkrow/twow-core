
#include "playerbot/playerbot.h"
#include "RogueTriggers.h"
#include "RogueActions.h"
#include "playerbot/RogueTankStancePolicy.h"

using namespace ai;

// twow-repo#654 (Rotation.RogueTankStance): the bot switch and the core switch (Funserver.Rogue.ShadowDanceStance) both cover it.
static bool RogueTankStanceCovers(Player* bot)
{
    return ai::rogue_tank_stance::Applies(sPlayerbotAIConfig.rotationRogueTankStance, bot->GetGUIDLow()) &&
        ai::rogue_tank_stance::CoreCoversBots(sWorld.getConfig(CONFIG_UINT32_FUNSERVER_ROGUE_SHADOW_DANCE_STANCE));
}

bool ShadowDanceTrigger::IsActive()
{
    if (!ai::rogue_tank_stance::Applies(sPlayerbotAIConfig.rotationRogueTankStance, bot->GetGUIDLow()))
        return BuffTrigger::IsActive();   // switch off: as before

    return RogueTankStanceCovers(bot) && bot->HasSpell(ai::rogue_tank_stance::StanceSpell) &&
        !bot->HasAura(ai::rogue_tank_stance::StanceSpell);
}

bool ShadowDanceStanceOffTrigger::IsActive()
{
    if (!bot->HasAura(ai::rogue_tank_stance::StanceSpell))
        return false;

    bool const tanking = ai->HasStrategy("tank rogue", BotState::BOT_STATE_COMBAT) || ai->HasStrategy("tank rogue", BotState::BOT_STATE_NON_COMBAT);
    return ai::rogue_tank_stance::Cancel(true, ai::rogue_tank_stance::WantsUp(true, RogueTankStanceCovers(bot), tanking));
}

bool RiposteCastTrigger::IsActive()
{
	Unit* target = GetTarget();
	if (!target)
		return false;

	bool isMelee = true;
	if (target->IsPlayer())
	{
		isMelee = !ai->IsRanged((Player*)target);
	}

	return SpellCanBeCastedTrigger::IsActive() && isMelee;
}