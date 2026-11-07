
#include "playerbot/playerbot.h"
#include "AreaTriggerAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/LockoutGuard.h"   // L1 (owner 07.11.2026): S3 split guard

using namespace ai;

bool ReachAreaTriggerAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    uint32 triggerId;

    if (ai->IsRealPlayer()) //Do not trigger own area trigger.
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    p >> triggerId;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
    if (!at)
    {
        WorldPacket p1(CMSG_AREATRIGGER);
        p1 << triggerId;
        p1.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p1);

        return true;
    }

    // L1 (owner 07.11.2026): S3 - no follow through a trigger into another copy of a raid the bot is perm-bound to, when
    // the requester already stands on that map (its map and instance id only).
    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, triggerId, requester))
    {
        ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);
        ai->TellError(requester, "I won't follow: I am locked to another copy of this raid");
        return true;
    }

    if (bot->GetMapId() != atEntry->mapid || bot->GetDistance(atEntry->x, atEntry->y, atEntry->z) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellError(requester, "I won't follow: too far away");
        return true;
    }

    MotionMaster &mm = *bot->GetMotionMaster();
	mm.MovePoint(atEntry->mapid, atEntry->x, atEntry->y, atEntry->z, FORCED_MOVEMENT_RUN);
    const float distance = sqrt(bot->GetDistance(atEntry->x, atEntry->y, atEntry->z, DIST_CALC_NONE));
    const float duration = 1000.0f * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;
    ai->TellError(requester, "Wait for me");
    SetDuration(duration);
    context->GetValue<LastMovement&>("last area trigger")->Get().lastAreaTrigger = triggerId;

    return true;
}



bool AreaTriggerAction::Execute(Event& event)
{
    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();

    uint32 triggerId = movement.lastAreaTrigger;
    movement.lastAreaTrigger = 0;

    // Module gate: while an exit-sensitive run owns this bot, a teleport
    // trigger underfoot must NOT be relayed (the consume above still clears
    // it so the trigger cannot fire later either). Non-teleport triggers are
    // not this action's business anyway - it returns before the relay for
    // those below.
    if (ai->IsAreaTriggerRelaySuppressed())
        return false;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTrigger const* at = sObjectMgr.GetAreaTrigger(triggerId);
    if (!at)
        return true;

    // L1 (owner 07.11.2026): S3 - the relay does not take the bot into another copy of a raid it is perm-bound to, when the
    // master already stands on that map. No reply: this relay never replies on failure.
    if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, triggerId, GetMaster()))
    {
        ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);
        return false;
    }

    WorldPacket p(CMSG_AREATRIGGER);
    p << triggerId;
    p.rpos(0);
    bot->GetSession()->HandleAreaTriggerOpcode(p);
    return true;
}
