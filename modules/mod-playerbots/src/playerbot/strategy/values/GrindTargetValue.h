#pragma once
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/strategy/Value.h"
#include "TargetValue.h"

namespace ai
{
   
    class GrindTargetValue : public TargetValue
	{
	public:
        GrindTargetValue(PlayerbotAI* ai, std::string name = "grind target") : TargetValue(ai, name, 2) {}

    public:
        Unit* Calculate() override;

    private:
        int GetTargetingPlayerCount(Unit* unit);
        Unit* FindTargetForGrinding(int assistCount);

        // #421: grey targets this bot engaged, reported once per hour.
        ObjectGuid lastGreyTarget;
        ai::quest_search::GreyEngagements greyEngagements;
    };
}
