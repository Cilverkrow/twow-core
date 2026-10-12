
#include "playerbot/playerbot.h"
#include "PriestTriggers.h"
#include "PriestActions.h"

using namespace ai;

bool InnerFireTrigger::IsActive()
{
    // twow-repo#568 (D22, AiPlayerbot.Diet.PriestSelfBuffKnown): inner fire is learned at 12; before that the
    // action ends IMPOSSIBLE (spell id 0, no alternative). Switch off: the old check only.
    if (sPlayerbotAIConfig.dietPriestSelfBuffKnown && !AI_VALUE2(uint32, "spell id", spell))
        return false;

    Unit* target = GetTarget();
    return SpellTrigger::IsActive() && !ai->HasAura(spell, target);
}

bool ShadowformTrigger::IsActive()
{
    // twow-repo#568 (D22): shadowform is a talent; without it the ACTION_MOVE node ended IMPOSSIBLE.
    if (sPlayerbotAIConfig.dietPriestSelfBuffKnown && !AI_VALUE2(uint32, "spell id", "shadowform"))
        return false;

    return !ai->HasAura("shadowform", bot);
}

bool InnerFocusTrigger::IsActive()
{
    // twow-repo#568 (D22): inner focus is a talent with a 3 min cooldown; CanCastSpell fails without the
    // spell and while it is not ready, and the node has no alternative.
    if (sPlayerbotAIConfig.dietPriestSelfBuffKnown)
    {
        const uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
        if (!spellId || !bot->IsSpellReady(spellId))
            return false;
    }

    return BuffTrigger::IsActive();
}

bool ShadowfiendTrigger::IsActive()
{
    return BoostTrigger::IsActive() && bot->IsSpellReady(34433);
}
