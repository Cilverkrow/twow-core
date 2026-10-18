#pragma once
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/GrindPickPolicy.h"
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
        // twow-repo#601 S2 diagnostic ([GrindPick], AiPlayerbot.BotUpdateTrace): the source FindTargetForGrinding picked its
        // target from, and the last target counted (a new pick counts once, not every recalculation of the value).
        ai::grind_pick::Source lastPickSource = ai::grind_pick::Source::None;
        ObjectGuid lastPickTarget;
    };
}
