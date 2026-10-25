
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/HunterMeleeStartPolicy.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "CombatActions.h"

using namespace ai;

bool SwitchToMeleeAction::isUseful()
{
    return ai->HasStrategy("ranged", BotState::BOT_STATE_COMBAT);
}

bool SwitchToMeleeAction::Execute(Event &event)
{
    if (Unit* target = AI_VALUE(Unit*, "current target"))
    {
        // twow-repo#602 (AiPlayerbot.Rotation.HunterMeleeStart, HunterMeleeStartPolicy.h): Player::MeleeAttackStart is a no-op stub
        // in this core; Unit::Attack with meleeAttack really starts the swing. Off = the stub call as before.
        if (ai::hunter_melee_start::Applies(sPlayerbotAIConfig.rotationHunterMeleeStart, bot->GetGUIDLow()))
        {
            if (bot->Attack(target, true))
                ai::hunter_melee_start::started.fetch_add(1, std::memory_order_relaxed);
        }
        else
            bot->MeleeAttackStart(target);
        return ChangeCombatStrategyAction::Execute(event);
    }
    return false;
}

bool SwitchToRangedAction::isUseful()
{

    return ai->HasStrategy("close", BotState::BOT_STATE_COMBAT);
}

bool SwitchToRangedAction::Execute(Event &event)
{
    if (Unit* target = AI_VALUE(Unit*, "current target"))
    {
        // twow-repo#602 (AiPlayerbot.Rotation.HunterMeleeStart): Player::MeleeAttackStop is a no-op stub too, so the swing kept
        // running in ranged mode; Unit::Attack(victim, false) does not stop it, so clear the melee state and tell the client.
        if (ai::hunter_melee_start::Applies(sPlayerbotAIConfig.rotationHunterMeleeStart, bot->GetGUIDLow()))
        {
            if (bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING))
            {
                bot->ClearUnitState(UNIT_STAT_MELEE_ATTACKING);
                bot->SendMeleeAttackStop(target);
                ai::hunter_melee_start::stopped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else
            bot->MeleeAttackStop(target);
        return ChangeCombatStrategyAction::Execute(event);
    }
    return false;
}
