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
        // twow-repo#601 S2 R4: round 0 (assistCount 0) writes its R4 skips to round0GapSkipped at the end of its candidate
        // loop; the caller's own local, so no shared state.
        Unit* FindTargetForGrinding(int assistCount, uint32& round0GapSkipped);
        // twow-repo#602 #2: the neighbour-aware replacement of the stage-2 distance pick (GrindNeighbourPolicy.h).
        struct NeighbourCandidate { Unit* unit; float distance; float botDistance; };
        Unit* NeighbourPick(Unit* pick, std::vector<NeighbourCandidate> const& candidates, std::list<ObjectGuid> const& targets, bool applies);

        // #421: grey targets this bot engaged, reported once per hour.
        ObjectGuid lastGreyTarget;
        ai::quest_search::GreyEngagements greyEngagements;
        // twow-repo#602 #2 (Grind.NeighbourAware): the last stage-2 distance pick and the last pick of the rule (the replacement, else
        // the distance pick), for the churn counters.
        ObjectGuid lastDistancePick;
        ObjectGuid lastNeighbourPick;
    };
}
