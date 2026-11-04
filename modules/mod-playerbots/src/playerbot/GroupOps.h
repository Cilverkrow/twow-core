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

// Any thread: the bot has an op in the queue (queued, or being run). The AI mark is the lock-free fast path (0 for every bot
// while OpQueue was never on); only a set mark asks the queue, and a mark the queue no longer knows is cleared.
bool StillPending(PlayerbotAI* ai, std::uint32_t guidLow);

// World thread, PlayerbotWorldScript::OnUpdate only.
void Drain();

// World thread: the ad-hoc manager (AdHoc.WorldThread) or S7 changed a group in this world tick - the drain runs no op in it.
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
