#pragma once

#include "playerbot/strategy/Trigger.h"

namespace ai
{
    // twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.Station / Cooking): at an anvil, forge or cooking fire the bot
    // crafts the focus recipes that ProfessionCraftTrigger leaves out (needsFocus). Active every 5 s while the travel
    // target is a StationTravelDestination in WORK; it picks the lowest-skill craftable recipe of the station's focus
    // (same backoff rule) and hands it to the existing "profession craft" action through "profession craft spell".
    // That action stamps "manual time profession craft", so a station craft also counts towards the craft interval of
    // the normal trigger. A no_focus result at the station ends the trip (OnTravelFailure, target expired).
    class StationCraftTrigger : public Trigger
    {
    public:
        StationCraftTrigger(PlayerbotAI* ai) : Trigger(ai, "station craft", 5) {}
        bool IsActive() override;
    };
}
