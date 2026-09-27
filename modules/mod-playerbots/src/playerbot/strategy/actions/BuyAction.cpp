
#include "playerbot/playerbot.h"
#include "playerbot/PerformanceMonitor.h"
#include "BuyAction.h"
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include <ctime>

using namespace ai;

namespace
{
vendor_gear::Settings VendorGearSettings()
{
    vendor_gear::Settings settings;
    settings.enabled = sPlayerbotAIConfig.vendorGearEnabled;
    settings.maxLevel = sPlayerbotAIConfig.vendorGearMaxLevel;
    settings.maxSpendPercent = sPlayerbotAIConfig.vendorGearMaxSpendPercent;
    settings.reserveCopper = sPlayerbotAIConfig.vendorGearReserveCopper;
    settings.cooldownSeconds = sPlayerbotAIConfig.vendorGearCooldownSeconds;
    return settings;
}

uint64 VendorGearNowMs()
{
    return uint64(time(nullptr)) * 1000;
}

void LogVendorGear(Player* bot, bool buy, char const* reason, uint32 itemId, uint32 slot, uint32 price,
    uint32 newScore, uint32 oldScore, uint32 allowance, uint32 spent)
{
    if (!buy && !sPlayerbotAIConfig.vendorGearTrace)
        return;

    sLog.outBasic("[VendorGear] %s reason=%s bot=%u level=%u item=%u slot=%u price=%u score_new=%u score_old=%u allowance=%u spent=%u",
        buy ? "buy" : "skip", reason, bot->GetGUIDLow(), bot->GetLevel(), itemId, slot, price, newScore, oldScore, allowance, spent);
}
}

bool BuyAction::VendorGearInScope(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    bool const rosterOnItsOwn = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
    return vendor_gear::InScope(VendorGearSettings(), rosterOnItsOwn, bot->GetLevel());
}

uint32 BuyAction::VendorGearAllowance(PlayerbotAI* ai)
{
    vendor_gear::Settings const settings = VendorGearSettings();
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();

    vendor_gear::Memory const memory = vendor_gear::Store::Instance().Get(bot->GetGUIDLow());
    if (vendor_gear::CooldownActive(settings, VendorGearNowMs(), memory.LastPurchaseMs()))
        return 0;

    // Repairs and class training come first; the rest may go into gear.
    uint64 reserve = uint64(settings.reserveCopper)
        + AI_VALUE2(uint32, "money needed for", (uint32)NeedMoneyFor::repair)
        + AI_VALUE2(uint32, "money needed for", (uint32)NeedMoneyFor::spells);
    if (reserve > UINT32_MAX)
        reserve = UINT32_MAX;

    return vendor_gear::VisitAllowance(settings, bot->GetMoney(), uint32(reserve));
}

bool BuyAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    bool buyUseful = false;
    ItemIds itemIds;
    std::string link = event.getParam();

    if (link == "vendor")
        buyUseful = true;
    else
    {
        itemIds = chat->parseItems(link);
    }

    std::list<ObjectGuid> vendors = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest npcs")->Get();
    bool vendored = false, result = false;

    UsageBoughtList bought;

    // twow-repo#363: gear purchases of a roster bot up to the level cap follow the
    // vendor gear policy instead of the "free money for gear" rule.
    bool const gearPolicy = buyUseful && VendorGearInScope(ai);
    uint32 const gearAllowance = gearPolicy ? VendorGearAllowance(ai) : 0;
    uint32 gearSpent = 0;
    vendor_gear::Memory gearMemory;
    bool gearCooldown = false;
    if (gearPolicy)
    {
        gearMemory = vendor_gear::Store::Instance().Get(bot->GetGUIDLow());
        gearMemory.BeginVisit();
        gearCooldown = vendor_gear::CooldownActive(VendorGearSettings(), VendorGearNowMs(), gearMemory.LastPurchaseMs());
    }

    for (std::list<ObjectGuid>::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature *pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature)
            continue;

        vendored = true;

        if (buyUseful)
        {
            //Items are evaluated from high-level to low level.
            //For each item the bot checks again if an item is useful.
            //Bot will buy until no useful items are left.

            VendorItemData const* tItems = pCreature->GetVendorItems();
            VendorItemData const* vItems = {};
#ifndef MANGOSBOT_ZERO                
            vItems = pCreature->GetVendorTemplateItems();
#endif
            if (!tItems && !vItems)
                continue;
            
            VendorItemList m_items_sorted;
            
            if (tItems)
                m_items_sorted.insert(m_items_sorted.begin(), tItems->m_items.begin(), tItems->m_items.end());
            if (vItems)
                m_items_sorted.insert(m_items_sorted.begin(), vItems->m_items.begin(), vItems->m_items.end());
            

            m_items_sorted.erase(std::remove_if(m_items_sorted.begin(), m_items_sorted.end(), [](VendorItem* i) {ItemPrototype const* proto = sObjectMgr.GetItemPrototype(i->item); return !proto; }), m_items_sorted.end());

            if (m_items_sorted.empty())
                continue;

            std::sort(m_items_sorted.begin(), m_items_sorted.end(), [](VendorItem* i, VendorItem* j) {return sObjectMgr.GetItemPrototype(i->item)->ItemLevel > sObjectMgr.GetItemPrototype(j->item)->ItemLevel; });

            for (auto& tItem : m_items_sorted)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(tItem->item);
                if (!proto)
                    continue;

                // reputation discount 
                uint32 price = uint32(floor(proto->BuyPrice * bot->GetReputationPriceDiscount(pCreature)));

                auto pmo = sPerformanceMonitor.start(PERF_MON_VALUE, "IsWorthBuyingFromVendorToResellAtAH", ai);

                // if item is worth selling to AH? 
                bool canFlipAH = ItemUsageValue::IsWorthBuyingFromVendorToResellAtAH(proto, tItem->maxcount > 0);

                pmo.reset();

#ifndef MANGOSBOT_ZERO
                const ItemExtendedCostEntry* iece = nullptr;
                if (tItem->ExtendedCost)
                {
                    iece = sItemExtendedCostStore.LookupEntry(tItem->ExtendedCost);
                    if (!iece)
                        continue;
                }
#endif

                for (uint32 n = 0; n < 10; ++n) //Buy 10 times or until no longer usefull/possible 
                {
                    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", tItem->item);

                    uint32 moneyKey = 0;
                    bool usageAllowed = true;
                    switch (usage)
                    {
                        case ItemUsage::ITEM_USAGE_EQUIP: moneyKey = (uint32)NeedMoneyFor::gear; break;
                        case ItemUsage::ITEM_USAGE_USE: moneyKey = (uint32)NeedMoneyFor::consumables; break;
                        case ItemUsage::ITEM_USAGE_SKILL: moneyKey = (uint32)NeedMoneyFor::tradeskill; break;
                        case ItemUsage::ITEM_USAGE_AMMO: moneyKey = (uint32)NeedMoneyFor::ammo; break;
                        case ItemUsage::ITEM_USAGE_QUEST:
                        case ItemUsage::ITEM_USAGE_FORCE_NEED:
                        case ItemUsage::ITEM_USAGE_FORCE_GREED: moneyKey = (uint32)NeedMoneyFor::anything; break;
                        case ItemUsage::ITEM_USAGE_AH:
                            usageAllowed = canFlipAH;
                            moneyKey = (uint32)NeedMoneyFor::anything;
                            break;
                        default: usageAllowed = false; break;
                    }
                    if (!usageAllowed)
                        break;

                    bool const policyGear = gearPolicy && usage == ItemUsage::ITEM_USAGE_EQUIP;
                    uint32 gearSlot = 0;
                    vendor_gear::Offer offer;
                    vendor_gear::Decision gearDecision;
                    if (policyGear)
                    {
                        offer.equipUpgrade = true;
                        offer.price = price;
                        offer.cooldownActive = gearCooldown;
                        offer.alreadyOwned = bot->HasItemCount(proto->ItemId, 1, false);
                        offer.boughtBefore = gearMemory.BoughtBefore(proto->ItemId);

                        uint16 dest = 0;
                        if (RandomPlayerbotMgr::CanEquipUnseenItem(bot, NULL_SLOT, dest, proto->ItemId) != EQUIP_ERR_OK)
                        {
                            LogVendorGear(bot, false, "cannot_equip_slot", proto->ItemId, 0, price, 0, 0, gearAllowance, gearSpent);
                            break;
                        }

                        gearSlot = dest & 255;
                        Item* oldItem = bot->GetItemByPos(dest);
                        ItemQualifier offered(proto->ItemId);
                        offer.slotEmpty = !oldItem;
                        offer.newScore = sRandomItemMgr.ItemStatWeight(bot, offered);
                        offer.oldScore = oldItem ? sRandomItemMgr.ItemStatWeight(bot, oldItem) : 0;
                        offer.slotBoughtThisVisit = gearMemory.SlotDone(gearSlot);

                        gearDecision = vendor_gear::Decide(offer, gearAllowance, gearSpent);
                        if (!gearDecision.buy)
                        {
                            LogVendorGear(bot, false, gearDecision.reason, proto->ItemId, gearSlot, price,
                                offer.newScore, offer.oldScore, gearAllowance, gearSpent);
                            break;
                        }
                    }
                    else
                    {
                        // Gold affordability 
                        RESET_AI_VALUE2(uint32, "free money for", moneyKey);
                        uint32 money = AI_VALUE2(uint32, "free money for", moneyKey);
                        if (price > money)
                            break;
                    }

#ifndef MANGOSBOT_ZERO
                    // ExtendedCost check
                    if (iece)
                    {
                        if (iece->reqhonorpoints && bot->GetHonorPoints() < iece->reqhonorpoints)
                            break;
                        if (iece->reqarenapoints && bot->GetArenaPoints() < iece->reqarenapoints)
                            break;

                        bool itemsOk = true;
                        for (uint8 k = 0; k < MAX_EXTENDED_COST_ITEMS; ++k)
                        {
                            if (iece->reqitem[k] && !bot->HasItemCount(iece->reqitem[k], iece->reqitemcount[k]))
                            {
                                itemsOk = false;
                                break;
                            }
                        }
                        if (!itemsOk)
                            break;
#ifdef MANGOSBOT_TWO
                        if (iece->reqpersonalarenarating && bot->GetMaxPersonalArenaRatingRequirement(iece->reqarenaslot) < iece->reqpersonalarenarating)
                            break;
#endif
                    }
#endif

                    if (usage == ItemUsage::ITEM_USAGE_USE && ItemUsageValue::CurrentStacks(ai, proto) >= 1)
                        break;

                    // Stop buying reagents/recipes once we have 1 stack
                    if (usage == ItemUsage::ITEM_USAGE_SKILL && ItemUsageValue::CurrentStacks(ai, proto) >= 1)
                        break;

                    bool didBuy = false;
                    didBuy = BuyItem(requester, tItems, vendorguid, proto, bought, usage);
                    if (!didBuy)
                        didBuy = BuyItem(requester, vItems, vendorguid, proto, bought, usage);

                    result |= didBuy;
                    if (!didBuy)
                    {
                        if (policyGear)
                            LogVendorGear(bot, false, "buy_failed", proto->ItemId, gearSlot, price,
                                offer.newScore, offer.oldScore, gearAllowance, gearSpent);
                        break;
                    }

                    if (policyGear)
                    {
                        gearSpent += price;
                        gearMemory.Record(proto->ItemId, gearSlot, VendorGearNowMs());
                        vendor_gear::Store::Instance().Put(bot->GetGUIDLow(), gearMemory);
                        LogVendorGear(bot, true, gearDecision.reason, proto->ItemId, gearSlot, price,
                            offer.newScore, offer.oldScore, gearAllowance, gearSpent);
                    }

                    RESET_AI_VALUE2(ItemUsage, "item usage", tItem->item);
                    RESET_AI_VALUE2(std::list<Item*>, "inventory items", ChatHelper::formatItem(proto));
                    RESET_AI_VALUE(std::vector<MountValue>, "mount list");

                    if (usage == ItemUsage::ITEM_USAGE_EQUIP || usage == ItemUsage::ITEM_USAGE_BAD_EQUIP) //Equip upgrades and stop buying this time. 
                    {
                        RESET_AI_VALUE2(ItemUsage, "item usage", tItem->item);
                        ai->DoSpecificAction("equip upgrades", event, true);
                        break;
                    }
                }
            }

            // Exception for crafting bots with guild order - Buy profession reagents from vendor first for more efficient crafting.
            if (AI_VALUE(bool, "needs profession reagents"))
            {
                std::vector<uint32> missingReagents = NeedsProfessionReagentsValue::GetMissingReagents(ai);

                for (uint32 reagentId : missingReagents)
                {
                    const ItemPrototype* reagentProto = sObjectMgr.GetItemPrototype(reagentId);
                    if (!reagentProto)
                        continue;

                    uint32 maxStack = reagentProto->GetMaxStackSize();
                    uint32 currentCount = ai->GetInventoryItemsCountWithId(reagentId);
                    if (currentCount >= maxStack)
                        continue;

                    uint32 reagentPrice = uint32(floor(reagentProto->BuyPrice * bot->GetReputationPriceDiscount(pCreature)));

                    while (currentCount < maxStack)
                    {
                        RESET_AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill);
                        uint32 money = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill);
                        if (reagentPrice > money)
                            break;

                        bool didBuy = BuyItem(requester, tItems, vendorguid, reagentProto, bought, ItemUsage::ITEM_USAGE_SKILL);
                        if (!didBuy)
                            didBuy = BuyItem(requester, vItems, vendorguid, reagentProto, bought, ItemUsage::ITEM_USAGE_SKILL);

                        result |= didBuy;
                        if (!didBuy)
                            break;

                        currentCount = ai->GetInventoryItemsCountWithId(reagentId);
                        RESET_AI_VALUE2(std::list<Item*>, "inventory items", ChatHelper::formatItem(reagentProto));
                    }
                }

                RESET_AI_VALUE(bool, "needs profession reagents");
            }
        }
        else
        {
            if (itemIds.empty())
                return false;

            for (ItemIds::iterator i = itemIds.begin(); i != itemIds.end(); i++)
            {
                uint32 itemId = *i;
                const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
                if (!proto)
                    continue;

                result |= BuyItem(requester, pCreature->GetVendorItems(), vendorguid, proto, bought);
                if (!result)
                    result |= BuyItem(requester, pCreature->GetVendorTemplateItems(), vendorguid, proto, bought);

                if (!result)
                {
                    std::ostringstream out; out << "Nobody sells " << ChatHelper::formatItem(proto) << " nearby";
                    ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                }
            }
        }
    }

    if (!vendored)
    {
        ai->TellError(requester, "There are no vendors nearby");
        return false;
    }
    else
    {
        for (auto& [usage, boughtList] : bought)
        {
            for (auto& [id, count] : boughtList)
            {                
                ItemQualifier qualifier(id);

                std::ostringstream out; 
                
                out << "Buying " << ChatHelper::formatItem(qualifier, count) << " ";
                out << ItemUsageValue::ReasonForNeed(usage, qualifier, count, bot);
                ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }
        }
    }

    return result;
}

bool BuyAction::BuyItem(Player* requester, VendorItemData const* tItems, ObjectGuid vendorguid, const ItemPrototype* proto, UsageBoughtList& bought, ItemUsage usage)
{
    uint32 oldCount = AI_VALUE2(uint32, "item count", proto->Name1);

    if (!tItems)
        return false;

    uint32 itemId = proto->ItemId;
    for (uint32 slot = 0; slot < tItems->GetItemCount(); slot++)
    {
        if (tItems->GetItem(slot)->item == itemId)
        {       
            uint32 botMoney = bot->GetMoney();
            if (ai->HasCheat(BotCheatMask::gold))
            {
                bot->SetMoney(10000000);
            }

#ifdef MANGOSBOT_TWO
            bot->BuyItemFromVendorSlot(vendorguid, slot, itemId, 1, NULL_BAG, NULL_SLOT);
#else
            bot->BuyItemFromVendor(vendorguid, itemId, 1, NULL_BAG, NULL_SLOT);
#endif
            if (ai->HasCheat(BotCheatMask::gold))
            {
                bot->SetMoney(botMoney);
            }

            if (oldCount < AI_VALUE2(uint32, "item count", proto->Name1)) //BuyItem Always returns false (unless unique) so we have to check the item counts.
            {
                sPlayerbotAIConfig.logEvent(ai, "BuyAction", proto->Name1, std::to_string(proto->ItemId));

                if (usage == ItemUsage::ITEM_USAGE_NONE)
                {

                    std::ostringstream out; out << "Buying " << ChatHelper::formatItem(proto);
                    ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                }
                else if (usage == ItemUsage::ITEM_USAGE_EQUIP) //We need to put these here since we are only buying 1 (hopefully) and need to report ReasonForNeed with old item still equiped.
                {
                    ItemQualifier qualifier(proto->ItemId);

                    std::ostringstream out;

                    out << "Buying " << ChatHelper::formatItem(qualifier) << " ";
                    out << ItemUsageValue::ReasonForNeed(usage, qualifier, 1, bot);
                    ai->TellPlayer(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                }
                else
                {
                    bought[usage][proto->ItemId]++;
                }
                return true;
            }
 
            return false;
        }
    }

    return false;
}

bool BuyBackAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();

    std::list<Item*> found = ai->InventoryParseItems(text, IterateItemsMask::ITERATE_ITEMS_IN_BUYBACK);

    //Sort items on itemLevel descending.
    found.sort([](Item* i, Item* j) {return i->GetProto()->ItemLevel > j->GetProto()->ItemLevel; });

    if (found.empty())
    {
        ai->TellError(requester, "No buyback items found");
        return false;
    }

    bool hasVendor = false;
    //Find vendor to interact with.
    ObjectGuid vendorguid;

    std::list<ObjectGuid> vendors = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest npcs")->Get();
    for (std::list<ObjectGuid>::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        vendorguid = *i;
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_VENDOR);
        if (!pCreature)
            continue;

        hasVendor = true;
        sServerFacade.SetFacingTo(bot, pCreature);
        break;
    }

    if (!hasVendor)
    {
        ai->TellError(requester, "There are no vendors nearby");
        return false;
    }

    bool result = false;

    for (auto& item : found)
    {
        uint32 slot = BUYBACK_SLOT_START;

        while (slot < BUYBACK_SLOT_END && bot->GetItemFromBuyBackSlot(slot) != item)
            slot++;

        if (slot == BUYBACK_SLOT_END)
            continue;

        uint32 price = bot->GetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + slot - BUYBACK_SLOT_START);
        if (bot->GetMoney() < price)
            continue;

        WorldPacket p1(CMSG_BUYBACK_ITEM);
        p1 << vendorguid;
        p1 << slot;
        bot->GetSession()->HandleBuybackItem(p1);
        if (bot->GetItemFromBuyBackSlot(slot) == nullptr)
            result = true;
    }

    return result;
}
