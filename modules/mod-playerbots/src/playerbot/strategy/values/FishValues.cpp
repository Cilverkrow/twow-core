
#include "playerbot/playerbot.h"
#include "FishValues.h"
#include "playerbot/FishingPolicy.h"

using namespace ai;

bool CanFishValue::Calculate()
{
    if (!bot->GetSkill(SKILL_FISHING, false, false)) //Unable to fish.
        return false;

    std::list<Item*> poles = AI_VALUE2(std::list<Item*>, "inventory items", "fishing pole");

    if (poles.empty()) //No fishing pole.
        return false;

    return true;
}

bool CanOpenFishingDobberValue::Calculate()
{
    if (!bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        return false;

    std::string spellName = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->m_spellInfo->SpellName[0];
    if (spellName.find("Fishing") != 0)
        return false;

    return true;
}

bool FishingInProgressValue::Calculate()
{
    if (Spell const* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if (std::string(channel->m_spellInfo->SpellName[0]).find("Fishing") == 0)
            return true;

    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los"))
    {
        GameObject* go = ai->GetGameObject(guid);
        if (go && go->GetEntry() == ai::fishing::BobberEntry && go->GetOwnerGuid() == bot->GetObjectGuid())
            return true;
    }
    return false;
}

bool DoneFishingValue::Calculate()
{
    if (!bot->GetSkill(SKILL_FISHING, false, false)) //Unable to fish.
        return false;

    // Hotfix 8.3: no weapon swap while the bobber is out or right after a cast - that
    // cancelled the fishing and made 118 bots swap pole and weapon every 1-3 minutes.
    if (AI_VALUE(bool, "fishing in progress") ||
        ai::fishing::InGrace(uint32(std::max(0, AI_VALUE2(int, "manual int", "last fishing cast"))), uint32(time(nullptr))))
        return false;

    Item* mhItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);

    //Does not have fishing pole equiped.
    if (!mhItem || mhItem->GetProto()->Class != ITEM_CLASS_WEAPON || mhItem->GetProto()->SubClass != ITEM_SUBCLASS_WEAPON_FISHING_POLE)
        return false;

    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        std::string spellName = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->m_spellInfo->SpellName[0];
        if (spellName.find("Fishing") == 0)
            return false;
    }

    return true;
}


