
#include "playerbot/playerbot.h"
#include "ShamanTriggers.h"
#include "ShamanActions.h"
#include "playerbot/ShamanImbuePolicy.h"

using namespace ai;

bool ShamanWeaponTrigger::IsActive()
{
    // twow-repo#541 (AiPlayerbot.Shaman.ImbueBySpec): the old loop's bug fixed - each learned imbue is checked,
    // not five times the trigger's own spell. The action then picks the imbue for the spec.
    if (sPlayerbotAIConfig.shamanImbueBySpec)
    {
        for (char const* imbue : { shaman_imbue::Rockbiter, shaman_imbue::Flametongue, shaman_imbue::Frostbrand, shaman_imbue::Windfury })
        {
            uint32 const imbueId = AI_VALUE2(uint32, "spell id", imbue);
            if (imbueId && AI_VALUE2(Item*, "item for spell", imbueId))
                return true;
        }
        return false;
    }

    // Audit A52 (twow-repo#563): the old loop ran five times over a lazily filled static list (a race between
    // region threads) but always checked the trigger's own spell, never the list entry - one check is the
    // identical result. Switch off: that result, unchanged.
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    return AI_VALUE2(Item*, "item for spell", spellId) != nullptr;
}

bool ShockTrigger::IsActive()
{
    return SpellTrigger::IsActive() && !ai->HasAnyAuraOf(GetTarget(), "frost shock", "earth shock", "flame shock", NULL) && !HasMaxDebuffs();
}
