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

    class CatchupQuestAction : public ChatCommandAction
    {
    public:
        CatchupQuestAction(PlayerbotAI* ai) : ChatCommandAction(ai, "catchup quest") {}
        bool Execute(Event& event) override;
        bool isUsefulWhenStunned() override { return true; }
    };
}
