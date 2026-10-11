
#include "playerbot/playerbot.h"
#include "HunterTriggers.h"
#include "HunterActions.h"

using namespace ai;

bool HunterNoStingsActiveTrigger::IsActive()
{
	Unit* target = AI_VALUE(Unit*, "current target");
    const bool noStings = target && AI_VALUE2(uint8, "health", "current target") > 40 &&
        !ai->HasAura("serpent sting", target) &&
        !ai->HasAura("scorpid sting", target) &&
        !ai->HasAura("viper sting", target);

    // twow-repo#568 (D10, AiPlayerbot.Diet.HunterBestSting): fire only when one sting of the node can be used, with
    // the same rules as CastViperStingAction / CastSerpentStingAction::isUseful under the switch. Viper sting is
    // learned at 36 and serpent sting at 4 (spell_template 3034 / 1978). Off: the old check only.
    if (!noStings || !sPlayerbotAIConfig.dietHunterBestSting)
        return noStings;

    const bool viperKnown = AI_VALUE2(uint32, "spell id", "viper sting") != 0;
    const bool targetHasMana = AI_VALUE2(uint8, "mana", "current target") >= 10;
    if (viperKnown && targetHasMana)
        return true;

    return AI_VALUE2(uint8, "health", "current target") > 50 && (!targetHasMana || !viperKnown) &&
        AI_VALUE2(uint32, "spell id", "serpent sting") != 0;
}

bool HuntersPetDeadTrigger::IsActive()
{
    return AI_VALUE(bool, "pet dead") && !AI_VALUE2(bool, "mounted", "self target");
}

bool HuntersPetLowHealthTrigger::IsActive()
{
    Unit* pet = AI_VALUE(Unit*, "pet target");
    return pet && AI_VALUE2(uint8, "health", "pet target") < 40 &&
        !AI_VALUE2(bool, "dead", "pet target") && !AI_VALUE2(bool, "mounted", "self target");
}

bool HunterPetNotHappy::IsActive()
{
    return !AI_VALUE(bool, "pet happy") && !AI_VALUE2(bool, "mounted", "self target");
}

bool ViperStingOnAttackerTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        const bool noStings = !ai->HasAura("serpent sting", target) &&
                              !ai->HasAura("scorpid sting", target) &&
                              !ai->HasAura("viper sting", target);
        if (noStings)
        {
            if (target->GetPower(POWER_MANA) >= 10)
            {
                return DebuffOnAttackerTrigger::IsActive();
            }
        }
    }

    return false;
}

bool SerpentStingOnAttackerTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        const bool noStings = !ai->HasAura("serpent sting", target) &&
                              !ai->HasAura("scorpid sting", target) &&
                              !ai->HasAura("viper sting", target);
        if (noStings)
        {
            if (target->GetPower(POWER_MANA) < 10)
            {
                return DebuffOnAttackerTrigger::IsActive();
            }
        }
    }

    return false;
}
