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

    // twow-repo#485 / #518 (role fill): a roster bot reports the role of its own talents at most every
    // AiPlayerbot.RosterGuild.SnapshotSeconds, so the guild deal knows it without touching its Player*
    // from another thread. Only with RosterGuild.Tanks/Healers/Dps or PlanFile set.
    class RosterGuildRoleTrigger : public Trigger {
    public:
        RosterGuildRoleTrigger(PlayerbotAI* ai) :
            Trigger(ai, "roster guild role", 60) {}

        bool IsActive() override;

    private:
        time_t lastRoleReport = 0;
    };
}