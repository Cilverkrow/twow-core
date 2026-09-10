#pragma once

#include "playerbot/strategy/Action.h"
#include "playerbot/BotDialogueProvider.h"
#include "QuestAction.h"

namespace ai
{
    class SayAction : public Action, public Qualified
    {
    public:
        SayAction(PlayerbotAI* ai);
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        virtual std::string getName() override { return "say::" + qualifier; }
        virtual bool isUsefulWhenStunned() override { return true; }

    private:
    };

    typedef std::pair<WorldPacket, uint32> delayedPacket;
    typedef std::vector<delayedPacket> delayedPackets;
    typedef std::future<delayedPackets> futurePackets;

    class ChatReplyAction : public Action
    {
    public:
        ChatReplyAction(PlayerbotAI* ai) : Action(ai, "chat message") {}
        virtual bool Execute(Event& event) override { return true; }
        bool isUseful() override;
        virtual bool isUsefulWhenStunned() override { return true; }

        static void GetAIChatPlaceholders(std::map<std::string, std::string>& placeholders, Unit* sender = nullptr, Unit* receiver = nullptr);
        static void GetAIChatPlaceholders(std::map<std::string, std::string>& placeholders, Unit* unit, const std::string preFix = "bot", Player* observer = nullptr);
        static WorldPacket GetPacketTemplate(OpcodesList op, uint32 type, Unit* sender, Unit* target = nullptr, std::string channelName = "");
        static delayedPackets LinesToPackets(const std::vector<std::string>& lines, WorldPacket packetTemplate,
            bool debug = false, uint32 MsPerChar = 0, WorldPacket emoteTemplate = WorldPacket(),
            uint32 timeDiff = 0, uint32 maxDelayMs = 0);

        static delayedPackets GenerateResponsePackets(const std::string json
            , const WorldPacket chatTemplate, const WorldPacket emoteTemplate, const WorldPacket systemTemplate, const std::string startPattern, const std::string endPattern, const std::string deletePattern, const std::string splitPattern, bool debug = false);

        // The same job as GenerateResponsePackets, for a bot whose voice comes
        // from a registered BotDialogueProvider instead of an LLMApiJson
        // request body. Runs on the async worker; `dialogue` is a copy of
        // scalars and strings, so nothing here can name a Player or a session.
        //
        // A provider answers with one line or with silence, so there is no
        // start/end/delete/split pattern here: those exist to carve chat out of
        // a completion API's response envelope, and there is no envelope.
        static delayedPackets GenerateDialoguePackets(const BotDialogueRequest dialogue
            , const WorldPacket chatTemplate, const WorldPacket emoteTemplate, const WorldPacket systemTemplate
            , bool debug, uint32 msPerCharacter, uint32 maxDelayMs);

        // This tree's own name for a chat source, as documented on
        // BotDialogueRequest::channel. Empty for SRC_UNDEFINED and nothing
        // else. Provider eligibility is applied separately before inference.
        static std::string DialogueChannelName(ChatChannelSource source);

        static void ChatReplyDo(Player* bot, uint32 type, uint32 guid1, uint32 guid2, std::string msg, std::string chanName, std::string name);
        static bool HandleThunderfuryReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool HandleToxicLinksReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool HandleWTBItemsReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool HandleLFGQuestsReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool SendGeneralResponse(Player* bot, ChatChannelSource chatChannelSource, std::string responseMessage, std::string name);
        static std::string GenerateReplyMessage(Player* bot, std::string incomingMessage, uint32 guid1, std::string name);
    };

    class SpeakAction : public Action, public Qualified
    {
    public:
        SpeakAction(PlayerbotAI* ai) : Action(ai, "speak"), Qualified() {};
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "speak"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This action wil make bots speak a certain line\n"
                   "Use \\p, \\1 \\y ect to make bots use different channels.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {""}; }
#endif    
    };
}
