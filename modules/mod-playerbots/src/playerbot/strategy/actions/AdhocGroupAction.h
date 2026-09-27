#pragma once
#include "playerbot/strategy/Action.h"
#include "playerbot/AdhocGroupPolicy.h"

class Group;

namespace ai
{
    // twow-repo#365 step 2 (design docs/design/bot-groups.md section 3): forms and
    // leaves ad-hoc quest groups. One instance per bot; its members are the bot's
    // own timers, so no AI context values are created per neighbour (#351).
    class AdhocGroupAction : public Action
    {
    public:
        AdhocGroupAction(PlayerbotAI* ai) : Action(ai, "ad-hoc group") {}
        bool Execute(Event& event) override;
        bool isUseful() override;

    private:
        adhoc_group::ObjectiveKey CurrentObjective(Player* player) const;
        bool IsCandidateBot(Player* player) const;
        void Scan(uint32 now);
        void RegisterPending(Group* group, uint32 now);
        bool CheckLeave(Group* group, uint32 now);
        void SendInvite(Player* inviter, Player* invitee) const;

        uint32 lastScan = 0;
        adhoc_group::ObjectiveKey pendingKey;
        uint32 pendingSince = 0;

        // Leave tracking for the current ad-hoc group.
        uint32 trackedGroup = 0;
        uint32 lastProgress = 0;
        uint32 lastCounter = 0;
        uint32 outOfRangeSince = 0;
        uint32 levelWindowSince = 0;
    };
}
