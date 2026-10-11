#pragma once

#include "playerbot/strategy/Trigger.h"

namespace ai
{
    class WorldPacketTrigger : public Trigger {
    public:
        WorldPacketTrigger(PlayerbotAI* ai, std::string command) : Trigger(ai, command), triggered(false) {}

        virtual void ExternalEvent(WorldPacket &packet, Player* owner = NULL) override
        {
            // Penqle's WorldPacket has a deleted copy operator=; copy-construct + move-assign instead.
            this->packet = WorldPacket(packet);
            this->owner = owner;
            triggered = true;
        }

        virtual Event Check() override
        {
            if (!triggered)
                return Event();

            return Event(getName(), packet, owner);
        }

        virtual void Reset() override
        {
            triggered = false;
        }

        // twow-repo#568 (D63): Check() reads this class's own (shadowing) flag, so report that one. The base flag
        // (set only by ExternalEventForce) stays ignored, as Check() ignores it. Engine::ProcessTriggers is the only
        // caller; with Perf.EventTriggerSkip = 0 its needCheck() is true for this interval-1 trigger anyway.
        virtual bool IsAlreadyTriggered() override { return triggered; }
        virtual bool IsEventOnly() override { return true; }
    private:
        WorldPacket packet;
        bool triggered;
        Player* owner;
    };
}
