#pragma once
#include "GenericActions.h"

namespace ai
{
    class GossipHelloAction : public ChatCommandAction
    {
    public:
        GossipHelloAction(PlayerbotAI* ai) : ChatCommandAction(ai, "gossip hello") {}
        virtual bool Execute(Event& event) override;

    private:
        struct GossipMenuSnapshot
        {
            uint32 menuId = 0;
            std::vector<std::string> items;
            bool operator==(GossipMenuSnapshot const& other) const { return menuId == other.menuId && items == other.items; }
        };

        void TellGossipMenus(Player* requester);
        bool ProcessGossip(Player* requester, ObjectGuid creatureGuid, int menuToSelect);
        void TellGossipText(Player* requester, uint32 textId);
        GossipMenuSnapshot SnapshotGossipMenu();
        bool ConfirmBindPoint(Player* requester, ObjectGuid innkeeperGuid, std::string const& chosen);

        // NPC whose menu the bot last opened; `talk N` only applies to that menu.
        ObjectGuid gossipNpc;
    };
}
