
#include "playerbot/playerbot.h"
#include "GossipHelloAction.h"

#include "playerbot/ServerFacade.h"
#include "AI/ScriptDevAI/ScriptDevAIMgr.h"


using namespace ai;

bool GossipHelloAction::Execute(Event& event)
{
	ObjectGuid guid;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
	WorldPacket &p = event.getPacket();
	if (p.empty())
	{
		if (requester)
			guid = requester->GetSelectionGuid();
	}
	else
	{
		p.rpos(0);
		p >> guid;
	}

	if (!guid)
		return false;

	Creature *pCreature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
	if (!pCreature)
	{
		DEBUG_LOG("[PlayerbotMgr]: HandleMasterIncomingPacket - Received  CMSG_GOSSIP_HELLO %s not found or you can't interact with him.", guid.GetString().c_str());
		return false;
	}

	GossipMenuItemsMapBounds pMenuItemBounds = sObjectMgr.GetGossipMenuItemsMapBounds(pCreature->GetCreatureInfo()->GossipMenuId);
	if (pMenuItemBounds.first == pMenuItemBounds.second)
		return false;

    std::string text = event.getParam();
	int menuToSelect = -1;
    if (event.getSource().find("rpg action") == 0)
    {
        Creature* pCreature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);

        if (pCreature)
        {
            if (!sScriptDevAIMgr.OnGossipHello(bot, pCreature))
            {
                bot->PrepareGossipMenu(pCreature, pCreature->GetDefaultGossipMenuId());
            }
        }

        gossipNpc = guid;
        ProcessGossip(requester, guid, -1);
    }
	else if (text.empty())
	{
        WorldPacket p1;
        p1 << guid;
        bot->GetSession()->HandleGossipHelloOpcode(p1);
        sServerFacade.SetFacingTo(bot, pCreature);
        gossipNpc = guid;

        std::ostringstream out; out << "--- " << pCreature->GetName() << " ---";
        ai->TellPlayerNoFacing(requester, out.str());

        TellGossipMenus(requester);
	}
	else
	{
        // `talk N` is a contract with the numbered list the bot printed: it must
        // run exactly that option of exactly that menu, or say why not. Never
        // fall back to a different option (atoi("x") used to become option 1).
        if (text.find_first_not_of("0123456789") != std::string::npos || text.size() > 3)
        {
            ai->TellError(requester, "Usage: talk <number from the list>");
            return false;
        }

        menuToSelect = atoi(text.c_str());
        if (menuToSelect < 1)
        {
            ai->TellError(requester, "Usage: talk <number from the list>");
            return false;
        }

        if (!bot->GetPlayerMenu() || gossipNpc != guid || !bot->GetPlayerMenu()->GetGossipMenu().MenuItemCount())
        {
            ai->TellPlayerNoFacing(requester, "I need to talk first");
            return false;
        }

        if (!ProcessGossip(requester, guid, menuToSelect - 1))
            return false;
	}

	bot->TalkedToCreature(pCreature->GetEntry(), pCreature->GetObjectGuid());
	return true;
}

void GossipHelloAction::TellGossipText(Player* requester, uint32 textId)
{
    if (!textId)
        return;

    GossipText const* text = sObjectMgr.GetGossipText(textId);
    if (text)
    {
        for (int i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; i++)
        {
            std::string text0 = text->Options[i].Text_0;
            if (!text0.empty()) ai->TellPlayerNoFacing(requester, text0);
            std::string text1 = text->Options[i].Text_1;
            if (!text1.empty()) ai->TellPlayerNoFacing(requester, text1);
        }
    }
}

void GossipHelloAction::TellGossipMenus(Player* requester)
{
    if (!bot->GetPlayerMenu())
        return;

     GossipMenu& menu = bot->GetPlayerMenu()->GetGossipMenu();

     if (requester)
     {
         Creature* pCreature = bot->GetNPCIfCanInteractWith(requester->GetSelectionGuid(), UNIT_NPC_FLAG_NONE);

         if (pCreature)
         {
             uint32 textId = bot->GetGossipTextId(menu.GetMenuId(), pCreature);
             TellGossipText(requester, textId);
         }
     }

    for (unsigned int i = 0; i < menu.MenuItemCount(); i++)
    {
        GossipMenuItem const& item = menu.GetItem(i);
        std::ostringstream out; out << "[" << (i+1) << "] " << item.m_gMessage;
        ai->TellPlayerNoFacing(requester, out.str());
    }
}

bool GossipHelloAction::ProcessGossip(Player* requester, ObjectGuid creatureGuid, int menuToSelect)
{
    GossipMenu& menu = bot->GetPlayerMenu()->GetGossipMenu();

    bool noFeedback = (menuToSelect == -1);

    if (!menu.MenuItemCount())
    {
        if (!noFeedback)
            ai->TellError(requester, "Unknown gossip option");
        return false;
    }

    int actualMenuToSelect = menuToSelect;

    if (actualMenuToSelect == -1)
    {
        actualMenuToSelect = urand(0, menu.MenuItemCount() - 1);
    }

    if (actualMenuToSelect < 0 || (unsigned int)actualMenuToSelect >= menu.MenuItemCount())
    {
        std::ostringstream out; out << "Unknown gossip option, choose 1-" << menu.MenuItemCount();
        ai->TellError(requester, out.str());
        return false;
    }

    // Copy what we need: selecting an option can rebuild or clear the menu.
    GossipMenuItem const item = menu.GetItem(actualMenuToSelect);
    GossipMenuSnapshot const before = SnapshotGossipMenu();

    WorldPacket p;
    std::string code;
    p << creatureGuid;
#ifdef MANGOSBOT_ZERO
    p << uint32(actualMenuToSelect);
#else
    p << menu.GetMenuId() << uint32(actualMenuToSelect);
#endif
    p << code;
    bot->GetSession()->HandleGossipSelectOptionOpcode(p);

    // The random rpg path only browses. Client confirmations below are for an
    // explicit player choice, otherwise wandering bots would rebind at every inn.
    if (noFeedback)
        return true;

    std::ostringstream chosen; chosen << "[" << (actualMenuToSelect + 1) << "] " << item.m_gMessage;

    switch (item.m_gOptionId)
    {
        case GOSSIP_OPTION_INNKEEPER:
            return ConfirmBindPoint(requester, creatureGuid, chosen.str());
        case GOSSIP_OPTION_VENDOR:
        case GOSSIP_OPTION_ARMORER:
            ai->TellPlayerNoFacing(requester, chosen.str() + " opens the vendor window - use 'b <item>' / 's <item>' instead");
            return true;
        case GOSSIP_OPTION_TRAINER:
            ai->TellPlayerNoFacing(requester, chosen.str() + " opens the trainer window - use 'trainer' instead");
            return true;
        case GOSSIP_OPTION_TAXIVENDOR:
            ai->TellPlayerNoFacing(requester, chosen.str() + " opens the flight map - use 'taxi' instead");
            return true;
        case GOSSIP_OPTION_BANKER:
            ai->TellPlayerNoFacing(requester, chosen.str() + " opens the bank window - use 'bank' instead");
            return true;
        case GOSSIP_OPTION_QUESTGIVER:
            ai->TellPlayerNoFacing(requester, chosen.str() + " opens the quest list - use 'quests' / 'accept' instead");
            return true;
        default:
            break;
    }

    // Only show a menu when the NPC actually opened a different one; a closed
    // gossip leaves the old items in place and must not be shown again.
    if (bot->GetPlayerMenu() && bot->GetPlayerMenu()->GetGossipMenu().MenuItemCount() && !(SnapshotGossipMenu() == before))
        TellGossipMenus(requester);
    else
        ai->TellPlayerNoFacing(requester, chosen.str() + " - done");

    return true;
}

GossipHelloAction::GossipMenuSnapshot GossipHelloAction::SnapshotGossipMenu()
{
    GossipMenuSnapshot snapshot;
    if (!bot->GetPlayerMenu())
        return snapshot;

    GossipMenu& menu = bot->GetPlayerMenu()->GetGossipMenu();
    snapshot.menuId = menu.GetMenuId();
    for (unsigned int i = 0; i < menu.MenuItemCount(); i++)
        snapshot.items.push_back(menu.GetItem(i).m_gMessage);

    return snapshot;
}

bool GossipHelloAction::ConfirmBindPoint(Player* requester, ObjectGuid innkeeperGuid, std::string const& chosen)
{
    // The core only asks the client (SMSG_BINDER_CONFIRM); a bot has no client
    // to answer, so answer the way the client does: CMSG_BINDER_ACTIVATE. That
    // handler keeps every gate (alive, in range, innkeeper, not in an instance).
    RESET_AI_VALUE(WorldPosition, "home bind"); // cached for 30s, read it fresh
    WorldPosition const oldHomeBind = AI_VALUE(WorldPosition, "home bind");

    WorldPacket activate;
    activate << innkeeperGuid;
    bot->GetSession()->HandleBinderActivateOpcode(activate);

    RESET_AI_VALUE(WorldPosition, "home bind");
    WorldPosition const newHomeBind = AI_VALUE(WorldPosition, "home bind");

    if (newHomeBind == oldHomeBind)
    {
        ai->TellError(requester, chosen + " - home bind unchanged (already bound here, or not allowed)");
        return false;
    }

    ai->TellPlayerNoFacing(requester, chosen + " - this inn is my new home");
    return true;
}
