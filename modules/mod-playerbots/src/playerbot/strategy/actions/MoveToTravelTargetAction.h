#pragma once

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/PathFailPolicy.h"

namespace ai
{
    class MoveToTravelTargetAction : public MovementAction {
    public:
        MoveToTravelTargetAction(PlayerbotAI* ai) : MovementAction(ai, "move to travel target") {}

        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    private:
        // twow-repo#575 (QFO 41747, owner 22.10.2026): the travel targets this bot already reported as [PathIsland].
        path_fail::IslandBook islandBook;
    };

}
