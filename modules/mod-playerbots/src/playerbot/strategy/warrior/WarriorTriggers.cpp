
#include "playerbot/playerbot.h"
#include "WarriorTriggers.h"
#include "WarriorActions.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

bool BloodrageBuffTrigger::IsActive()
{
    return AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth &&
        AI_VALUE2(uint8, "rage", "self target") < 20;
}

bool SunderArmorDebuffTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (ai->IsTank(bot) && !target->IsPlayer())
        return true;

    return !ai->HasAura("sunder armor", target, true) && !HasMaxDebuffs();
}

bool CommandingShoutTrigger::IsActive()
{
    uint32 comShout = AI_VALUE2(uint32, "spell id", "commanding shout");
    uint32 batShout = AI_VALUE2(uint32, "spell id", "battle shout");
    if (!(comShout || batShout))
        return false;

    if (bot->HasSpell(comShout))
        return !ai->HasAura("commanding shout", bot);

    if (bot->HasSpell(batShout))
        return !ai->HasAura("battle shout", bot);

    return false;
}

bool DisarmDebuffTrigger::IsActive()
{
    // twow-repo#568 D37: Disarm (676) has a 60 s cooldown and needs Defensive Stance, but the debuff check
    // alone was true on almost every protection tick (IMPOSSIBLE 54 / OK 0). On: only a known, ready Disarm
    // in Defensive Stance fires. Off: the plain debuff check as before.
    if (sPlayerbotAIConfig.dietWarriorDisarmGate)
    {
        const uint32 spellId = AI_VALUE2(uint32, "spell id", "disarm");
        if (!spellId || !bot->IsSpellReady(spellId) || !ai->HasAura("defensive stance", bot))
            return false;
    }

    return DebuffTrigger::IsActive();
}

bool BerserkerStanceTrigger::IsActive()
{
    // twow-repo#568 D38: Berserker Stance is a level-30 quest spell. On: a bot that does not know it
    // never pushes it (traces: PREREQ 30 / IMPOSSIBLE 32 / OK 0). Off: the plain buff check as before.
    if (sPlayerbotAIConfig.dietWarriorBerserkerStanceGate && !AI_VALUE2(uint32, "spell id", "berserker stance"))
        return false;

    return BuffTrigger::IsActive();
}

bool DefensiveTacticsBerserkerStanceTrigger::IsActive()
{
    if (!BerserkerStanceTrigger::IsActive())
        return false;
    if (!sServerFacade.IsInCombat(bot))
        return false;

    // twow-repo#568 D38: the Defensive Tactics swap only keeps threat with the talent (51606-51608,
    // "while wearing a shield"). On: no swap without a rank of the talent or without a shield.
    if (sPlayerbotAIConfig.dietWarriorBerserkerStanceGate)
    {
        if (!bot->HasSpell(51606) && !bot->HasSpell(51607) && !bot->HasSpell(51608))
            return false;

        Item* offhand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        if (!offhand || offhand->GetProto()->Class != ITEM_CLASS_ARMOR || offhand->GetProto()->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD)
            return false;
    }

    return AI_VALUE2(uint8, "health", "self target") > 70;
}
