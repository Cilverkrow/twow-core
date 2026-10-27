#pragma once

#include "playerbot/strategy/Action.h"

#include "playerbot/BotSlots.h"
#include "playerbot/BotGroupDiagnostics.h"
#include "playerbot/GroupAiRefresh.h"
#include "playerbot/GroupOpTiming.h"
namespace ai
{
    class AcceptInvitationAction : public Action
    {
    public:
        AcceptInvitationAction(PlayerbotAI* ai) : Action(ai, "accept invitation") {}

        virtual bool Execute(Event& event) override
        {
            // s7r (BotGroups.WorldThreadDefer): the tail a world-thread manager posted (ApplyGroupAiRequests, bot thread). The
            // join itself is done; the owner is that join's inviter, still a member of the bot's group.
            if (event.getSource() == group_ai_refresh::JoinTailSource)
            {
                Player* inviter = event.getOwner();
                Group* joined = bot->GetGroup();
                if (!inviter || !joined)
                    return false;
                JoinTail(joined, inviter, event, /*onBotThread*/ true);
                return true;
            }

            Group* grp = bot->GetGroupInvite();
            if (!grp)
                return false;

            Player* inviter = sObjectMgr.GetPlayer(grp->GetLeaderGuid());
            if (!inviter)
                return false;

			if (!ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE, false, inviter))
            {
                WorldPacket data(SMSG_GROUP_DECLINE, 10);
                data << bot->GetName();
                sServerFacade.SendPacket(inviter, data);
                bot->UninviteFromGroup();
                return false;
            }
            
            if (bot->isAFK())
                bot->ToggleAFK();

            WorldPacket p;
            uint32 roles_mask = 0;
            p << roles_mask;
            auto const coreStart = std::chrono::steady_clock::now();
            bot->GetSession()->HandleGroupAcceptOpcode(p);
            group_op_timing::lastAcceptCoreUs = group_op_timing::UsSince(coreStart);   // s7r: the core join alone

            if (!bot->GetGroup() || !bot->GetGroup()->IsMember(inviter->GetObjectGuid()))
                return false;

            // twow-repo#628 / #627: a world-thread manager (ad-hoc AdhocGroupWorld, S7 QuestGroupWorld) runs this accept. The strategy
            // rebuild and the reset below are the expensive part (a join or leave cost ~10 ms of the world tick in the C short test, 24.10.2026);
            // they run in the bot's next update on its own thread instead (PlayerbotAI::ApplyGroupAiRequests, same sequence).
            bool const worldManager = group_ai_refresh::IsWorldManagerSource(event.getSource());

            if (sRandomPlayerbotMgr.IsFreeBot(bot))
            {
                ai->SetMaster(inviter);

                if (!worldManager)
                {
                    std::string defaultMovementStrategy = ai->GetDefaultMovementStrategy();
                    ai->ChangeStrategy("+" + defaultMovementStrategy, BotState::BOT_STATE_NON_COMBAT);
                }
            }

            if (worldManager)
            {
                // s7r (BotGroups.WorldThreadDefer): with the reset the rest of this action goes to the bot's own thread too - the
                // world thread keeps the core join (s7r: accept 1-2.4 ms with logs, chat, update gear on the world thread).
                if (sPlayerbotAIConfig.botGroupsWorldThreadDefer)
                {
                    ai->RequestJoinTail(inviter->GetGUIDLow());   // JoinReset + JoinTail, the inviter before the bits
                    return true;
                }
                ai->RequestGroupAi(group_ai_refresh::JoinReset);
            }
            else
            {
                ai->ResetStrategies();

                ai->ChangeStrategy("-lfg,-bg", BotState::BOT_STATE_NON_COMBAT);
                ai->Reset();
            }

            JoinTail(grp, inviter, event);
            return true;
        }

    private:
        // Everything after the join and the reset: the event and membership log, the invite chat, the master's formation, the
        // hello, reset raids, update gear. Here at once - or, posted by a world-thread manager, on the bot's own thread.
        void JoinTail(Group* grp, Player* inviter, Event& event, bool onBotThread = false)
        {
            sPlayerbotAIConfig.logEvent(ai, "AcceptInvitationAction", grp->GetLeaderName(), std::to_string(grp->GetMembersCount()));
            bot_group::LogMembership(bot, bot->GetGroup(), "join");

            Player* master = inviter;

            if (GetBotAI(master)) //Copy formation from bot master.
            {
                if (sPlayerbotAIConfig.inviteChat && (sRandomPlayerbotMgr.IsFreeBot(bot) || !ai->HasActivePlayerMaster()))
                {
                    std::map<std::string, std::string> placeholders;
                    placeholders["%name"] = master->GetName();
                    std::string reply;
                    if (urand(0, 3))
                        reply = BOT_TEXT2("Send me an invite %name!", placeholders);
                    else
                        reply = BOT_TEXT2("Sure I will join you.", placeholders);

                    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());

                    if (guild && master->IsInGuild(bot->GetGuildId()))
                        guild->BroadcastToGuild(bot->GetSession(), reply, LANG_UNIVERSAL);
                    else if (sServerFacade.GetDistance2d(bot, master) < sPlayerbotAIConfig.spellDistance * 1.5)
                        bot->Say(reply, (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                }

                // s7r pre-review: on the bot's own thread the master's AI value is read only when the master shares this map
                // (the same map thread); otherwise the bot keeps its own formation.
                if (!onBotThread || master->FindMap() == bot->FindMap())
                {
                    Formation* masterFormation = MAI_VALUE(Formation*, "formation");
                    FormationValue* value = (FormationValue*)context->GetValue<Formation*>("formation");
                    value->Load(masterFormation->getName());
                }
            }

            ai->TellPlayer(inviter, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

            ai->DoSpecificAction("reset raids", event, true);
            ai->DoSpecificAction("update gear", event, true);
        }

    public:
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "accept invitation"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot accept group invitations.\n"
                   "It will automatically handle AFK status and update strategies.\n"
                   "For free bots, the inviter becomes the bot's master.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {"reset raids", "update gear"}; }
        virtual std::vector<std::string> GetUsedValues() { return {"formation"}; }
#endif 
    };

}
