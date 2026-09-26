#pragma once

#include "playerbot/strategy/Trigger.h"
#include "playerbot/ZoneEscapePolicy.h"

namespace ai
{
    zone_escape::Facts GatherZoneEscapeFacts(PlayerbotAI* ai);

    // #307: a roster bot on its own stands in a zone clearly above its level.
    class ZoneEscapeTrigger : public Trigger
    {
    public:
        ZoneEscapeTrigger(PlayerbotAI* ai) : Trigger(ai, "zone escape", 10) {}
        bool IsActive() override;
    };
}
