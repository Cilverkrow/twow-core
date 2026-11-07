
#include "playerbot/playerbot.h"
#include "TeleportAction.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/LockoutGuard.h"   // L1 (owner 07.11.2026): S3 split guard

using namespace ai;

bool TeleportAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::list<ObjectGuid> gos = *context->GetValue<std::list<ObjectGuid> >("nearest game objects no los");
    for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
    {
        GameObject* go = ai->GetGameObject(*i);
        if (!go)
            continue;

        GameObjectInfo const *goInfo = go->GetGOInfo();
        if (goInfo->type != GAMEOBJECT_TYPE_SPELLCASTER)
            continue;

        uint32 spellId = goInfo->spellcaster.spellId;
        const SpellEntry* const pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (pSpellInfo->Effect[0] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[1] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[2] != SPELL_EFFECT_TELEPORT_UNITS)
            continue;

        if (!bot->GetGameObjectIfCanInteractWith(go->GetObjectGuid(), MAX_GAMEOBJECT_TYPE))
            continue;

        std::ostringstream out; out << "Teleporting using " << goInfo->name;
        ai->TellPlayerNoFacing(requester, out.str());

        ai->ChangeStrategy("-follow,+stay", BotState::BOT_STATE_NON_COMBAT);

        ai->UseGameObjectDirect(*i);   // Hotfix 8.8 (#474)
        return true;
    }

    LastMovement& movement = context->GetValue<LastMovement&>("last area trigger")->Get();
    if (movement.lastAreaTrigger)
    {
        // L1 (owner 07.11.2026): S3 - no relay into another copy of a raid the bot is perm-bound to, when the requester
        // already stands on that map (its map and instance id only). The trigger stays for a later try.
        if (sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly && ai::lockout_guard::RelaySplit(bot, movement.lastAreaTrigger, requester))
        {
            ai::lockout_guard::Count(ai::lockout_guard::SplitEntry);
            ai->TellPlayerNoFacing(requester, "I am locked to another copy of this raid");
            return false;
        }

        WorldPacket p(CMSG_AREATRIGGER);
        p << movement.lastAreaTrigger;
        p.rpos(0);

        bot->GetSession()->HandleAreaTriggerOpcode(p);
        movement.lastAreaTrigger = 0;
        return true;
    }

    ai->TellPlayerNoFacing(requester, "Cannot find any portal to teleport");
    return false;
}
