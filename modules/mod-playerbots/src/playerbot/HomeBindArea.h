#pragma once

// twow-repo#596 D (AiPlayerbot.Perf.BindAreaFromHomebind): the zone-escape trigger (every 10 s per roster bot) and the
// hearthstone check asked WorldPosition::GetArea() of the home bind - a terrain lookup (getAreaFlag -> GetGrid ->
// LoadMapAndVMap) at a position that is often far from every player, so its grid is loaded from disk again and again:
// bot Zinkbolt (soak 17.10.) spent 126 ms in "zone escape" about once a minute. The core keeps the bind spot's area
// since the bind (Player::GetHomebindAreaId); with the switch that area is used instead - the same area, the same
// level (TravelMgr::GetAreaLevel by area id), no terrain. Off: as before.

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/WorldPosition.h"

namespace ai::home_bind
{
    inline AreaTableEntry const* Area(Player* bot, WorldPosition const& bind)
    {
        if (sPlayerbotAIConfig.perfBindAreaFromHomebind && bot && bot->GetHomebindAreaId())
            if (AreaTableEntry const* area = AreaEntry::GetById(bot->GetHomebindAreaId()))
                return area;
        return bind.GetArea();
    }

    // As WorldPosition::getAreaLevel() of the bind, without the terrain lookup when the switch is on.
    inline int32 AreaLevel(Player* bot, WorldPosition const& bind)
    {
        if (!sPlayerbotAIConfig.perfBindAreaFromHomebind)
            return bind.getAreaLevel();
        if (bind.getMapId() == 609)
            return 1;
        AreaTableEntry const* area = Area(bot, bind);
        return area ? sTravelMgr.GetAreaLevel(area->ID) : 0;
    }
}
