#pragma once
#include "playerbot/strategy/Value.h"
#include "NearestUnitsValue.h"
#include "playerbot/PlayerbotAIConfig.h"

namespace ai
{
    class NearestFriendlyPlayersValue : public NearestUnitsValue
	{
	public:
        NearestFriendlyPlayersValue(PlayerbotAI* ai, float range = sPlayerbotAIConfig.sightDistance) :
            NearestUnitsValue(ai, "nearest friendly players", range) {}

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        virtual bool AcceptUnit(Unit* unit) override;
        // twow-repo#541 (audit A03): AcceptUnit is a GUID compare (pure).
        bool AcceptUnitBeforeLos() const override { return true; }
	};
}
