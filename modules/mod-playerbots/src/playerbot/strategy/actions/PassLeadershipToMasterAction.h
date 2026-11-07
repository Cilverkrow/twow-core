#pragma once
#include "GenericActions.h"
#include "playerbot/GroupOps.h"   // A3 lever 2: BotGroups.WritePath

namespace ai
{
    class PassLeadershipToMasterAction : public ChatCommandAction
    {
    public:
        PassLeadershipToMasterAction(PlayerbotAI* ai, std::string name = "leader", std::string message = "Passing leader to you!") : ChatCommandAction(ai, name), message(message) {}

        virtual Player* PassLeaderTo(Event& event) { return GetMaster(); };

        virtual bool Execute(Event& event) override
        {
            Player* passLeaderTo = PassLeaderTo(event);
            if (passLeaderTo && passLeaderTo != bot && bot->GetGroup() && bot->GetGroup()->IsMember(passLeaderTo->GetObjectGuid()))
            {
                // A3 lever 2 (AiPlayerbot.BotGroups.WritePath): an in-scope bot's handover is a world-thread op (the set-leader
                // opcode there, GroupOps.cpp RunLeader); the reset of a random bot follows it on this bot's thread (LeaderReset).
                // The message goes out with the decision. Refused, or an op of the bot pending: false, decided again later.
                if (group_ops::WritePathRoutes(ai))
                {
                    if (group_ops::StillPending(ai, bot->GetGUIDLow()) ||
                        !group_ops::QueueLeader(ai, passLeaderTo, sRandomPlayerbotMgr.IsRandomBot(bot)))
                        return false;
                    if (!message.empty())
                        ai->TellPlayerNoFacing(passLeaderTo, message);
                    return true;
                }

                WorldPacket p(SMSG_GROUP_SET_LEADER, 8);
                p << passLeaderTo->GetObjectGuid();
                bot->GetSession()->HandleGroupSetLeaderOpcode(p);
                
                if (!message.empty())
                    ai->TellPlayerNoFacing(passLeaderTo, message);

                if (sRandomPlayerbotMgr.IsRandomBot(bot))
                {
                    ai->ResetStrategies();
                    ai->Reset();
                }
                
                return true;
            }

            return false;
        }

        virtual bool isUseful() override
        {
            return ai->IsAlt() && bot->GetGroup() && bot->GetGroup()->IsLeader(bot->GetObjectGuid());
        }

        virtual bool isUsefulWhenStunned() override { return true; }

    protected:
        std::string message;
    };

    class GiveLeaderAction : public PassLeadershipToMasterAction 
    {
    public:
        GiveLeaderAction(PlayerbotAI* ai, std::string message = "Lead the way!") : PassLeadershipToMasterAction(ai, "give leader", message) {}

        virtual Player* PassLeaderTo(Event& event) { return event.getOwner(); };

        virtual bool isUseful() override
        {
            return bot->GetGroup() && bot->GetGroup()->IsLeader(bot->GetObjectGuid());
        }        
    };    
}
