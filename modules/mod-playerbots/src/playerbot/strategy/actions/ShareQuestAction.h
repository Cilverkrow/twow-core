#pragma once
#include "GenericActions.h"

namespace ai
{
    class ShareQuestAction : public ChatCommandAction
    {
    public:
        ShareQuestAction(PlayerbotAI* ai, std::string name = "share quest") : ChatCommandAction(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };

    class AutoShareQuestAction : public ShareQuestAction
    {
    public:
        AutoShareQuestAction(PlayerbotAI* ai) : ShareQuestAction(ai, "auto share quest") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

        virtual bool isUseful() override { return bot->GetGroup() && !ai->HasActivePlayerMaster(); }
    };

    // #340/core#102: one admission for a roster bot catching up on its
    // master's quest, used by the chat command and by the normal share button.
    enum class CatchupResult
    {
        ADMITTED, ALREADY_HAS, NOT_MASTER, NOT_ROSTER, NO_QUEST, MASTER_LACKS_QUEST,
        NOT_SHAREABLE, NOT_SAME_GROUP, RAID, TOO_FAR, LEVEL_WINDOW, CANT_TAKE, LOG_FULL,
    };

    class CatchupQuestAction : public ChatCommandAction
    {
    public:
        CatchupQuestAction(PlayerbotAI* ai) : ChatCommandAction(ai, "catchup quest") {}
        bool Execute(Event& event) override;
        bool isUsefulWhenStunned() override { return true; }

        // Every gate of core#102; AddQuest only when all of them pass.
        static CatchupResult Admit(Player* bot, Player* master, Player* requester, uint32 questId);
        static char const* ResultCode(CatchupResult result);
    };
}
