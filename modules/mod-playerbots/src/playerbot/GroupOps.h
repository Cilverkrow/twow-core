#pragma once

#include "playerbot/GroupOpQueue.h"

#include <cstdint>

class PlayerbotAI;

namespace ai
{
class Action;
}

// BOT-12 (twow-repo#677, #628; AiPlayerbot.BotGroups.OpQueue): the game side of the world-thread group-operation queue
// (GroupOpQueue.h / GroupOpPolicy.h). Producers on the bots' map threads call Enqueue; PlayerbotWorldScript::OnUpdate calls
// Drain every world tick, after sMapMgr.Update (all maps idle). Drain runs the core join/leave paths of the ad-hoc manager
// there; the AI tail (strategy reset, accept tail, log lines) is always posted to the bot's own thread (GroupAiRefresh.h
// LeaveReset / JoinReset|JoinTail, DeferredLog.h).
namespace ai::group_ops
{
Queue& Ops();

// Any thread. Pushes the op (enqueue time set here) and, when queued, marks the bot - and for a join the inviter - pending
// (PlayerbotAI::GroupOpPending) until the world thread ran or dropped it. otherAi: the inviter's AI (Join), else null.
Pushed Enqueue(Op& op, PlayerbotAI* botAi, PlayerbotAI* otherAi);

// The pending mark (PlayerbotAI::groupOpPending) is a HINT: the lock-free fast path (0 for every bot while OpQueue was never
// on); the queue's own record (Queue::IsPending) is the truth. The mark is set before the push and cleared by seq when the op
// ran or was dropped. A mark can briefly read set without a queued op (a push in flight, or a refused push being restored) and,
// in a rare cross-thread race, read clear while an op is queued; the queue's dedup (RefusedPending) keeps both safe - at worst
// the bot decides once more and the push is refused.
//
// The bot's own thread (AdhocGroupAction::Execute): its op waits. A mark the queue no longer knows (its op ran or was dropped
// while the bot was not found) is cleared - by the seq read here, so a newer mark set meanwhile survives.
bool StillPending(PlayerbotAI* ai, std::uint32_t guidLow);

// Any thread (a neighbour's scan): the bot has an op waiting. Never clears a mark (that is the owner's job, above).
bool PendingHint(PlayerbotAI* ai, std::uint32_t guidLow);

// Map thread, the old ad-hoc path (AdhocGroupAction::SendInvite, OpQueue off for both bots): tags the invitee with its inviter
// so the off-thread observer counts the invitee's accept - the group create - as the ad-hoc path. Only while the observer is
// on (BotGroups.Diagnostics or OpQueue); one relaxed store.
void NoteAdhocInvite(PlayerbotAI* inviteeAi, std::uint32_t inviterGuidLow);

// World thread, PlayerbotWorldScript::OnUpdate only.
void Drain();

// World thread: the ad-hoc manager (AdHoc.WorldThread: a scan slice, an invite or a leave) or S7 (a scan slice, a dissolution or
// a group operation) did group work in this world tick - the drain runs no op in it. Not marked (world-thread group work that
// can share a tick with one drained op): mod-dungeon-clear DcTestRunJob grouping, LoginFreeBots "create group", RandomizeFirst.
void MarkHeavyTick();

// The outermost bot action running on this thread (Engine::ListenAndExecute), read by the off-thread observer to attribute a
// group create/disband to its calling path. One pointer pair per thread, set and cleared around Action::Execute.
inline thread_local PlayerbotAI* t_actionAi = nullptr;
inline thread_local Action* t_outerAction = nullptr;

class ActionScope
{
public:
    ActionScope(PlayerbotAI* ai, Action* action) : outer(t_outerAction == nullptr)
    {
        if (outer)
        {
            t_actionAi = ai;
            t_outerAction = action;
        }
    }
    ~ActionScope()
    {
        if (outer)
        {
            t_actionAi = nullptr;
            t_outerAction = nullptr;
        }
    }
    ActionScope(ActionScope const&) = delete;
    ActionScope& operator=(ActionScope const&) = delete;

private:
    bool outer;
};

// Registers the GroupScript that counts group creates/disbands off the world thread (AddSC_playerbot_hooks).
void AddGroupOpsObserver();
}
