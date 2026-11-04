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
        // twow-repo#602 #2: the neighbour-aware replacement of the stage-2 distance pick (GrindNeighbourPolicy.h).
        struct NeighbourCandidate { Unit* unit; float distance; float botDistance; };
        Unit* NeighbourPick(Unit* pick, std::vector<NeighbourCandidate> const& candidates, std::list<ObjectGuid> const& targets, bool applies);
        // twow-repo#656 (Grind.SafePull): the risk-ordered pick over the same candidates (SafePullPolicy.h); null = the bot rests.
        Unit* SafePick(Unit* pick, std::vector<NeighbourCandidate> const& candidates, std::list<ObjectGuid> const& targets, bool applies);

        // #421: grey targets this bot engaged, reported once per hour.
        ObjectGuid lastGreyTarget;
        ai::quest_search::GreyEngagements greyEngagements;
        // twow-repo#602 #2 (Grind.NeighbourAware): the last stage-2 distance pick and the last pick of the rule (the replacement, else
        // the distance pick), for the churn counters.
        ObjectGuid lastDistancePick;
        ObjectGuid lastNeighbourPick;
        // twow-repo#656 (Grind.SafePull): the first pass of the current readiness wait (unix seconds, 0 = not waiting).
        uint32 safeWaitSince = 0;
    };
}
