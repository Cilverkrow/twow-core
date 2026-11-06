#pragma once

#include "playerbot/strategy/Action.h"
#include "playerbot/DuelRulePolicy.h"

namespace ai
{
    class AcceptDuelAction : public Action
    {
    public:
        AcceptDuelAction(PlayerbotAI* ai) : Action(ai, "accept duel")
        {}

        virtual bool Execute(Event& event) override
        {
            WorldPacket p(event.getPacket());

            ObjectGuid flagGuid;
            p >> flagGuid;
            ObjectGuid playerGuid;
            p >> playerGuid;

            // twow-repo#602 W24 duel hang (AiPlayerbot.Rotation.DuelRule, DuelRulePolicy.h): the challenger gets SMSG_DUEL_REQUESTED too,
            // but the core ignores the initiator's accept (HandleDuelAcceptedOpcode) - only the challenged player accepts.
            bool const duelRule = ai::duel_rule::Applies(sPlayerbotAIConfig.rotationDuelRule, bot->GetGUIDLow());
            if (ai::duel_rule::SkipInitiatorAccept(duelRule, bot->m_duel && bot->m_duel->initiator == bot))
            {
                ai::duel_rule::skippedAccept.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            // do not auto duel with low hp or below certain level
            if (bot->GetLevel() < sPlayerbotAIConfig.botAcceptDuelMinimumLevel
                || ((!ai->HasRealPlayerMaster() || (ai->GetMaster() && ai->GetMaster()->GetObjectGuid() != playerGuid)) && AI_VALUE2(uint8, "health", "self target") < 90))
            {
                WorldPacket packet(CMSG_DUEL_CANCELLED, 8);
                packet << flagGuid;
                bot->GetSession()->HandleDuelCancelledOpcode(packet);

                // twow-repo#602 W24 (DuelRule): a declined duel stops here; before, the accept below was sent after the cancel.
                if (duelRule)
                    return true;
            }

            WorldPacket packet(CMSG_DUEL_ACCEPTED, 8);
            packet << flagGuid;
            bot->GetSession()->HandleDuelAcceptedOpcode(packet);

            ai->ResetStrategies();
            return true;
        }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "accept duel"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This action accepts or declines duel invitations based on conditions.\n"
                "The bot will decline if below minimum level or low health (unless from master).\n"
                "After accepting, combat strategies are reset for the duel.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    };

    // twow-repo#602 W24 duel hang (AiPlayerbot.Rotation.DuelRule, DuelRulePolicy.h): "duel stalled" - a bot-vs-bot duel without a fight
    // for Rotation.DuelRule.CancelSeconds. Cancels it like the client's cancel / forfeit button (CMSG_DUEL_CANCELLED): a requested duel
    // ends as interrupted, a running one as forfeit.
    class CancelDuelAction : public Action
    {
    public:
        CancelDuelAction(PlayerbotAI* ai) : Action(ai, "cancel duel")
        {}

        virtual bool isUseful() override
        {
            return bot->m_duel && !bot->m_duel->finished;
        }

        virtual bool Execute(Event& event) override
        {
            WorldPacket packet(CMSG_DUEL_CANCELLED, 8);
            packet << bot->GetGuidValue(PLAYER_DUEL_ARBITER);
            bot->GetSession()->HandleDuelCancelledOpcode(packet);
            ai::duel_rule::cancelled.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "cancel duel"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This action cancels a bot-vs-bot duel in which neither side fought for a while.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };

}
