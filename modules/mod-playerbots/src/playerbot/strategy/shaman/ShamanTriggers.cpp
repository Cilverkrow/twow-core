
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
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
    // twow-repo#568 D26 (AiPlayerbot.Diet.ShockCooldownGate): Earth, Flame and Frost Shock share one spell category
    // (6 s). While it runs the pushed Earth Shock and its Flame Shock alternative can only end IMPOSSIBLE, so the
    // trigger stays quiet. Without a known Earth Shock (id 0) the old check runs unchanged.
    if (sPlayerbotAIConfig.dietShockCooldownGate)
    {
        uint32 const shockId = AI_VALUE2(uint32, "spell id", "earth shock");
        if (shockId)
        {
            SpellEntry const* shockInfo = sServerFacade.LookupSpellInfo(shockId);
            if (shockInfo && (bot->HasSpellCooldown(shockId) || bot->HasSpellCategoryCooldown(shockInfo->Category)))
                return false;
        }
    }

    return SpellTrigger::IsActive() && !ai->HasAnyAuraOf(GetTarget(), "frost shock", "earth shock", "flame shock", NULL) && !HasMaxDebuffs();
}
