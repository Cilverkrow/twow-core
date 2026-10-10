
#include "playerbot/playerbot.h"
#include "NearestFriendlyPlayersValue.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

void NearestFriendlyPlayersValue::FindUnits(std::list<Unit*> &targets)
{
    AnyFriendlyUnitInObjectRangeCheck u_check(bot, range);
    UnitListSearcher<AnyFriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
    // twow-repo#541 (audit A03, AiPlayerbot.Perf.NearestUnitsAcceptFirst): players live only in the world
    // container (GridDefines.h AllWorldObjectTypes). VisitAllObjects walks the grid container first and
    // the world container second, and AcceptUnit keeps only players, so visiting the world container
    // alone yields the same players in the same order without checking every grid creature.
    if (sPlayerbotAIConfig.nearestUnitsAcceptFirst)
        Cell::VisitWorldObjects(bot, searcher, range);
    else
        Cell::VisitAllObjects(bot, searcher, range);
}

bool NearestFriendlyPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->GetObjectGuid();
    return guid.IsPlayer() && guid != ai->GetBot()->GetObjectGuid();
}
