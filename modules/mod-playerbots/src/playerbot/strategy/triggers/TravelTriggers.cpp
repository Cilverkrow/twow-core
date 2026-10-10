
#include "playerbot/playerbot.h"
#include "TravelTriggers.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/TravelRequestPolicy.h"
#include "playerbot/ServerFacade.h"
using namespace ai;

bool HasNearbyQuestTakerTrigger::IsActive()
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK) //We are not currently working on a target.
        return false;

    if (target->GetExpiredTime() < 2 * MINUTE) //The target was set more than 2 minutes ago.
        return false;

    return AI_VALUE(bool, "has nearby quest taker");
}

bool NearDarkPortalTrigger::IsActive()
{
    return sServerFacade.GetAreaId(bot) == 72;
}

bool AtDarkPortalAzerothTrigger::IsActive()
{
    if (sServerFacade.GetAreaId(bot) == 72)
    {
        if (sServerFacade.GetDistance2d(bot, -11906.9f, -3208.53f) < 20.0f)
        {
            return true;
        }
    }
    return false;
}

bool AtDarkPortalOutlandTrigger::IsActive()
{
    if (sServerFacade.GetAreaId(bot) == 3539)
    {
        if (sServerFacade.GetDistance2d(bot, -248.1939f, 921.919f) < 10.0f)
        {
            return true;
        }
    }
    return false;
}

bool TravelRequestTrigger::IsActive()
{
    // Same name as ValueTrigger: the qualifier is the event source and the stored travel condition.
    name = getQualifier();

    // twow-repo#541 (audit A18): the request actions are useless while the target is prepared or active
    // (RequestTravelTargetAction::isUseful), so their condition is not read then.
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
    bool const prepare = target && target->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE;
    bool const active = !prepare && AI_VALUE(bool, "travel target active");
    if (!travel_request::MayCheck(prepare, active))
        return false;

    return AI_VALUE(bool, getQualifier());
}
