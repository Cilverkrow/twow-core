
#include "playerbot/playerbot.h"
#include "RogueActions.h"
#include "playerbot/AiFactory.h"

using namespace ai;

bool ApplyAgitatingPoisonAction::isPossible()
{
    // #367: the bot poison 90140 has no source on purpose (soulbound, bots only,
    // core#188). A roster rogue tank from level 20 gets one stack when it has none.
    uint32 const botPoison = 90140;
    if (bot->GetLevel() >= 20 && !bot->HasItemCount(botPoison, 1) &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        AiFactory::GetPremadePathName(bot) == "rogue tank")
    {
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(botPoison))
        {
            uint32 const count = std::max<uint32>(1, proto->GetMaxStackSize());
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, botPoison, count) == EQUIP_ERR_OK &&
                bot->StoreNewItemInInventorySlot(botPoison, count))
                sLog.outBasic("[SpecAura] state=kit_item bot=%u level=%u item=%u count=%u",
                    bot->GetGUIDLow(), bot->GetLevel(), botPoison, count);
        }
    }

    return ApplyPoisonAction::isPossible();
}
