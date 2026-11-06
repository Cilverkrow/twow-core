#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
    class CurrentTargetValue : public UnitManualSetValue
	{
	public:
        CurrentTargetValue(PlayerbotAI* ai, std::string name = "current target") : UnitManualSetValue(ai, nullptr, name) {}
        virtual Unit* Get() override;
        virtual void Set(Unit* unit) override;
        // twow-repo#602 W24 (log only, [StuckDetail] cur_target_*): the stored selection - Get() hides a target the bot no longer sees.
        ObjectGuid GetSelection() const { return selection; }

    private:
        ObjectGuid selection;
    };
}
