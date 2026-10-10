#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class HasNearbyQuestTakerTrigger : public Trigger
    {
    public:
        HasNearbyQuestTakerTrigger(PlayerbotAI* ai) : Trigger(ai, "has nearby quest taker", 30) {}

        virtual bool IsActive() override;
    };

    class NearDarkPortalTrigger : public Trigger
    {
    public:
        NearDarkPortalTrigger(PlayerbotAI* ai) : Trigger(ai, "near dark portal", 10) {}

        virtual bool IsActive() override;
    };

    class AtDarkPortalAzerothTrigger : public Trigger
    {
    public:
        AtDarkPortalAzerothTrigger(PlayerbotAI* ai) : Trigger(ai, "at dark portal azeroth", 10) {}

        virtual bool IsActive() override;
    };

    class AtDarkPortalOutlandTrigger : public Trigger
    {
    public:
        AtDarkPortalOutlandTrigger(PlayerbotAI* ai) : Trigger(ai, "at dark portal outland", 10) {}

        virtual bool IsActive() override;
    };

    // twow-repo#541 (audit A18): "travel request::<condition>" - a "val::<condition>" trigger for the travel
    // request actions that does not read <condition> while the travel target is prepared or active
    // (RequestTravelTargetAction::isUseful rejects every request then). Name and event source are the
    // qualifier, exactly as ValueTrigger. Only created when AiPlayerbot.Perf.TravelRequestGate = 1.
    class TravelRequestTrigger : public Trigger, public Qualified
    {
    public:
        TravelRequestTrigger(PlayerbotAI* ai) : Trigger(ai, "travel request", 1), Qualified() {}

        virtual bool IsActive() override;
    };
}
