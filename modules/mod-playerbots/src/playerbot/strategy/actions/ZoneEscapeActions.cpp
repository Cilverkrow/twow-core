#include "playerbot/playerbot.h"
#include "ZoneEscapeActions.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/triggers/ZoneEscapeTriggers.h"

using namespace ai;

bool ZoneEscapeAction::Execute(Event& event)
{
    zone_escape::Facts const facts = GatherZoneEscapeFacts(ai);
    zone_escape::Decision const decision = zone_escape::Decide(facts);
    if (decision.step == zone_escape::Step::None)
        return false;

    // Every attempt starts the cooldown, successful or not: no loops.
    SET_AI_VALUE2(time_t, "manual time", "zone escape", time(nullptr));

    char const* state = "travel";
    char const* reason = decision.reason;
    bool done = false;

    if (decision.step == zone_escape::Step::Hearth)
    {
        done = ai->DoSpecificAction("hearthstone", Event("zone escape"), true);
        if (done)
            state = "hearth";
        else
            reason = "hearth_failed";
    }

    if (!done)
    {
        // A new target is chosen under the normal level and route rules
        // (#132/#134/#138, danger map #140); the current one led here.
        if (TravelTarget* target = AI_VALUE(TravelTarget*, "travel target"))
            target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        done = true;
    }

    sLog.outBasic("[ZoneEscape] state=%s reason=%s bot=%u level=%u area_level=%u map=%u zone=%u",
        state, reason, bot->GetGUIDLow(), facts.botLevel, facts.areaLevel, bot->GetMapId(), bot->GetZoneId());
    return done;
}
