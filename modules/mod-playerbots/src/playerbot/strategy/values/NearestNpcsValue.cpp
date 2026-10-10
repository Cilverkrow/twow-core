
#include "playerbot/playerbot.h"
#include "NearestNpcsValue.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#ifdef MANGOSBOT_TWO
#include "Entities/Vehicle.h"
#endif

using namespace ai;
using namespace MaNGOS;

void NearestNpcsValue::FindUnits(std::list<Unit*> &targets)
{
    AnyUnitInObjectRangeCheck u_check(bot, range);
    UnitListSearcher<AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

// twow-repo#541 (audit A03, AiPlayerbot.Perf.NearestUnitsAcceptFirst): with the switch on, players are
// rejected before the hostility check. Same result (both terms are pure), but the player-vs-player
// reaction (other player's duel, group, reputation) is no longer computed just to be discarded.
bool NearestNpcsValue::AcceptUnit(Unit* unit)
{
    if (sPlayerbotAIConfig.nearestUnitsAcceptFirst && dynamic_cast<Player*>(unit))
        return false;

    return !sServerFacade.IsHostileTo(unit, bot) && !dynamic_cast<Player*>(unit);
}

void NearestVehiclesValue::FindUnits(std::list<Unit*>& targets)
{
    AnyUnitInObjectRangeCheck u_check(bot, range);
    UnitListSearcher<AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestVehiclesValue::AcceptUnit(Unit* unit)
{
#ifdef MANGOSBOT_TWO
    if (!unit || !unit->IsVehicle() || !unit->IsAlive())
        return false;

    VehicleInfo* veh = unit->GetVehicleInfo();
    if (!veh->CanBoard(bot))
        return false;

    return true;
#endif

    return false;
}
