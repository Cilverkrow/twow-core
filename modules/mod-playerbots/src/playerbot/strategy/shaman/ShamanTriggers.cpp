
#include "playerbot/playerbot.h"
#include "ShamanTriggers.h"
#include "ShamanActions.h"

using namespace ai;

bool ShamanWeaponTrigger::IsActive()
{
    // Audit A52 (twow-repo#563): the old loop ran five times over a lazily filled static list (a race between
    // region threads) but always checked the trigger's own spell, never the list entry - one check is the
    // identical result. Checking each weapon spell would be a behaviour change and is not done here.
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    return AI_VALUE2(Item*, "item for spell", spellId) != nullptr;
}

bool ShockTrigger::IsActive()
{
    return SpellTrigger::IsActive() && !ai->HasAnyAuraOf(GetTarget(), "frost shock", "earth shock", "flame shock", NULL) && !HasMaxDebuffs();
}
