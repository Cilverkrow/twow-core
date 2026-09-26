#pragma once

#include "playerbot/strategy/Action.h"

namespace ai
{
    // #307: leave a zone clearly above the bot's level (hearthstone, otherwise
    // a new level-appropriate travel target).
    class ZoneEscapeAction : public Action
    {
    public:
        ZoneEscapeAction(PlayerbotAI* ai) : Action(ai, "zone escape") {}
        bool Execute(Event& event) override;
    };
}
