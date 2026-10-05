#pragma once
#include "playerbot/strategy/Trigger.h"

namespace ai
{	
    class PetitionTurnInTrigger : public Trigger {
    public:
        PetitionTurnInTrigger(PlayerbotAI* ai) :
            Trigger(ai, "petition turn in trigger", 5) {}

        bool IsActive() override { return AI_VALUE(bool, "can hand in petition"); };
    };

    class BuyTabardTrigger : public Trigger {
    public:
        BuyTabardTrigger(PlayerbotAI* ai) :
            Trigger(ai, "buy tabard trigger", 5) {}

        bool IsActive() override { return AI_VALUE(bool, "can buy tabard"); };
    };

    class LeaveLargeGuildTrigger : public Trigger {
    public:
        LeaveLargeGuildTrigger(PlayerbotAI* ai) :
            Trigger(ai, "leave large guild trigger", 10) {}

        bool IsActive();
    };

    // twow-repo#485 (owner 04.10.): a roster bot in a guild checks its guild note at most every
    // AiPlayerbot.RosterGuild.NoteRefreshSeconds (time compare only; the trigger itself is looked at
    // once a minute). The action writes the note only when it changed.
    class RosterGuildNoteTrigger : public Trigger {
    public:
        RosterGuildNoteTrigger(PlayerbotAI* ai) :
            Trigger(ai, "roster guild note", 60) {}

        bool IsActive() override;

    private:
        time_t lastNoteCheck = 0;
    };
}