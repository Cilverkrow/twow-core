#pragma once

#include "playerbot/strategy/Action.h"

namespace ai
{
    // #333: runs one "craft random item" (bot as target, so only recipes
    // without spell focus) and records the attempt for the craft interval.
    class ProfessionCraftAction : public Action
    {
    public:
        ProfessionCraftAction(PlayerbotAI* ai) : Action(ai, "profession craft") {}
        bool Execute(Event& event) override;
    };
}
