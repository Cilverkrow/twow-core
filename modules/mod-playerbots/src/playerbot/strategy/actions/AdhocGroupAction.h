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
        void Scan(uint32 now);
        void RegisterPending(Group* group, uint32 now);
        bool CheckLeave(Group* group, uint32 now);
        void SendInvite(Player* inviter, Player* invitee) const;
        // BOT-12 (twow-repo#677, BotGroups.OpQueue): the leave / the invite as an op of the world-thread group queue.
        bool EnqueueLeave(Group* group, adhoc_group::Registry::Entry const& entry, adhoc_group::Leave reason, uint32 now);
        bool EnqueueJoin(Player* inviter, Player* invitee, adhoc_group::ObjectiveKey const& key);

        uint32 lastScan = 0;
        adhoc_group::ObjectiveKey pendingKey;
        uint32 pendingSince = 0;

        // Leave tracking for the current ad-hoc group.
        uint32 trackedGroup = 0;
        uint32 lastProgress = 0;
        uint32 lastCounter = 0;
        uint32 outOfRangeSince = 0;
        uint32 levelWindowSince = 0;
        uint32 leaderOtherMapSince = 0;   // BOT-12 (BotGroups.LeaderOtherMap)
        uint32 opRetryAt = 0;             // BOT-12 (BotGroups.OpQueue): the queue refused the last leave - not before this
    };
}
