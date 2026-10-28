#include "playerbot/playerbot.h"
#include "ZoneEscapeActions.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/triggers/ZoneEscapeTriggers.h"
#include "playerbot/TeleportCastWait.h"   // twow-repo#333 finding 4: hearthstone cast wait

using namespace ai;

bool ZoneEscapeAction::Execute(Event& event)
{
    // twow-repo#333 finding 4: each run starts from the react delay, so neither the wait step's 5 s nor a hearthstone hold
    // carries over to a later travel step.
    SetDuration(sPlayerbotAIConfig.reactDelay);
    zone_escape::Facts const facts = GatherZoneEscapeFacts(ai);
    zone_escape::Decision const decision = zone_escape::Decide(facts);
    if (decision.step == zone_escape::Step::None)
        return false;

    zone_escape::Step const lastStep = zone_escape::Step(AI_VALUE2(int, "manual int", "zone escape step"));
    SET_AI_VALUE2(int, "manual int", "zone escape step", int(decision.step));

    // The current travel target led here; whatever happens next must not be
    // another local target (train 5: the next lake the bot died at).
    if (TravelTarget* target = AI_VALUE(TravelTarget*, "travel target"))
    {
        // Train 6: and the target that led here is not picked again right
        // after the hearthstone (Durotar -> Grull Hawkwind through the Barrens).
        target->SuppressCurrentDestination(sPlayerbotAIConfig.destinationDeathsCooldownSeconds * IN_MILLISECONDS);
        target->TagCurrentSuppression(destination_death::SourceEscape);   // twow-repo#601 F5 (diagnostic)
        target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
    }

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
    if (decision.step == zone_escape::Step::Travel && facts.failedHearths)
        SET_AI_VALUE2(int, "manual int", "zone escape fails", 0);

    if (decision.step == zone_escape::Step::Hearth)
    {
        // Counts attempts; reset once the bot is out of the zone (trigger).
        SET_AI_VALUE2(int, "manual int", "zone escape fails", int(facts.failedHearths + 1));
        if (ai->DoSpecificAction("hearthstone", Event("zone escape"), true))
        {
            state = lastStep == zone_escape::Step::Hearth ? "retry" : "hearth";
            // twow-repo#333 finding 4 (TeleportCastPolicy.h): hold for the started cast (the engine sets the AI wait to this
            // action's duration); the watch ends the hold when the spell stops PREPARING.
            SetDuration(teleport_cast::HoldFor(ai, bot, teleport_cast::kHearthstone, sPlayerbotAIConfig.reactDelay));
        }
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
