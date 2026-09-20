
#include "playerbot/playerbot.h"
#include "SetHomeAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"

using namespace ai;

bool SetHomeAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    ObjectGuid selection = bot->GetSelectionGuid();
    bool isRpgAction = AI_VALUE(GuidPosition, "rpg target") == selection;

    auto setHome = [&](Creature* innkeeper)
    {
        WorldPosition const oldHomeBind = AI_VALUE(WorldPosition, "home bind");
        bot->GetSession()->SendBindPoint(innkeeper);
        ai->TellPlayer(requester, "This inn is my new home", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        RESET_AI_VALUE(WorldPosition, "home bind");

        if (sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions &&
            sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        {
            WorldPosition const newHomeBind = AI_VALUE(WorldPosition, "home bind");
            sLog.outBasic("[QuestFirstRoute] state=homebind bot=%u old_map=%u old_zone=%u new_map=%u new_zone=%u reason=innkeeper_bind",
                bot->GetGUIDLow(), oldHomeBind.getMapId(),
                sTerrainMgr.GetZoneId(oldHomeBind.getMapId(), oldHomeBind.getX(), oldHomeBind.getY(), oldHomeBind.getZ()),
                newHomeBind.getMapId(),
                sTerrainMgr.GetZoneId(newHomeBind.getMapId(), newHomeBind.getX(), newHomeBind.getY(), newHomeBind.getZ()));
        }

        return true;
    };

    if (!isRpgAction)
    {
        if (requester)
        {
            selection = requester->GetSelectionGuid();
        }
        else
        {
            return false;
        }
    }

    if (selection)
    {
        Unit* unit = ai->GetUnit(selection);
        if (unit && unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_INNKEEPER))
        {
            if (isRpgAction)
            {
                return setHome(ai->GetCreature(selection));
            }
            else
            {
                return setHome(ai->GetCreature(selection));
            }
        }
    }

    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
    {
        Creature *unit = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_INNKEEPER);
        if (!unit)
            continue;

        return setHome(unit);
    }

    ai->TellPlayer(requester, "Can't find any innkeeper around");
    return false;
}
