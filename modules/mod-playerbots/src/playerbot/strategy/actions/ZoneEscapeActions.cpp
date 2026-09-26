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

    zone_escape::Step const lastStep = zone_escape::Step(AI_VALUE2(int, "manual int", "zone escape step"));
    SET_AI_VALUE2(int, "manual int", "zone escape step", int(decision.step));

    // The current travel target led here; whatever happens next must not be
    // another local target (train 5: the next lake the bot died at).
    if (TravelTarget* target = AI_VALUE(TravelTarget*, "travel target"))
        target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);

    if (decision.step == zone_escape::Step::Wait)
    {
        // Hold still until the hearthstone is ready; the trigger re-checks
        // every few seconds. Logged once per waiting phase.
        ai->StopMoving();
        SetDuration(5000);
        if (lastStep != zone_escape::Step::Wait)
            sLog.outBasic("[ZoneEscape] state=wait reason=%s bot=%u level=%u area_level=%u map=%u zone=%u",
                decision.reason, bot->GetGUIDLow(), facts.botLevel, facts.areaLevel, bot->GetMapId(), bot->GetZoneId());
        return true;
    }

    // Every hearth or travel attempt starts the interval (short retry after a
    // hearthstone attempt, full cooldown otherwise): no loops.
    SET_AI_VALUE2(time_t, "manual time", "zone escape", time(nullptr));

    char const* state = "travel";
    char const* reason = decision.reason;

    if (decision.step == zone_escape::Step::Hearth)
    {
        if (ai->DoSpecificAction("hearthstone", Event("zone escape"), true))
            state = lastStep == zone_escape::Step::Hearth ? "retry" : "hearth";
        else
        {
            reason = "hearth_failed";
            SET_AI_VALUE2(int, "manual int", "zone escape step", int(zone_escape::Step::Travel));
        }
    }

    sLog.outBasic("[ZoneEscape] state=%s reason=%s bot=%u level=%u area_level=%u map=%u zone=%u",
        state, reason, bot->GetGUIDLow(), facts.botLevel, facts.areaLevel, bot->GetMapId(), bot->GetZoneId());
    return true;
}
