#pragma once

#include "playerbot/strategy/Trigger.h"

namespace ai
{
    // #333: throttled [ProfessionUse] diagnostics for roster bots (BASIC level,
    // AiPlayerbot.ProfessionUse.Trace).
    void TraceProfessionUse(PlayerbotAI* ai, char const* stage, char const* state, char const* reason, uint32 detail);

    bool IsRosterBotOnItsOwn(PlayerbotAI* ai);

    // twow-repo#485: not moving (nor on a flight path), fighting, casting,
    // sitting or mounted - a craft cast would fail now.
    bool IsReadyToCraft(PlayerbotAI* ai);

    // twow-repo#485 / hotfix 8.16a: reagents and tools from the bags, regardless
    // of the item cheat (also used by "craft random item", the rpg craft path).
    bool HasCraftTools(SpellEntry const* spell, Player* bot);
    uint32 CraftableFromBags(SpellEntry const* spell, Player* bot);

    // #333: minimal crafting loop. Active for a roster bot on its own when a
    // recipe without spell focus still gives a skill-up and its reagents are in
    // the bags, at most once per AiPlayerbot.ProfessionUse.CraftIntervalSeconds.
    class ProfessionCraftTrigger : public Trigger
    {
    public:
        ProfessionCraftTrigger(PlayerbotAI* ai) : Trigger(ai, "profession craft", 10) {}
        bool IsActive() override;
    };
}
