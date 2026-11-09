#pragma once

#include "playerbot/GroupOpQueue.h"
#include "playerbot/GroupManagerPolicy.h"   // A3 step 6 T30-0: group_mgr::GroupType of a manager join

#include <cstddef>
#include <cstdint>
#include <vector>

class PlayerbotAI;
class Player;

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

// ---- A3 lever 2: the group write path (ADR-0041 amendment 4.2, AiPlayerbot.BotGroups.WritePath, default 0) ----------------
// Design: cli-grp/a3-hebel2/DESIGN-A3-HEBEL2-SCHREIBWEG-20261107.md. With the key on for a bot, every group membership change
// its own map thread would make (leave, accept/decline, invite, leader) is queued instead and runs in Drain on the world
// thread (RunLeave / RunAccept / RunInvite / RunLeader, GroupOps.cpp); the AI tail goes back to the bot's thread.

// "join"/"lfg" chat commands (JoinGroupAction, LfgAction) leave, invite and accept in one action - an order one op per bot
// cannot keep. They keep the direct path inside this scope (set only in those two Execute bodies); the observer counts the
// writes in it as bypass. on = false (the key at 0): no scope.
inline thread_local std::uint32_t t_writeBypass = 0;

class WritePathBypass
{
public:
    explicit WritePathBypass(bool on) : active(on)
    {
        if (active)
            ++t_writeBypass;
    }
    ~WritePathBypass()
    {
        if (active)
            --t_writeBypass;
    }
    WritePathBypass(WritePathBypass const&) = delete;
    WritePathBypass& operator=(WritePathBypass const&) = delete;

private:
    bool active;
};

// The producers' gate (any map thread): the key on, the bot in scope (WritePathApplies: cohort; a bot AI without a real-player
// master, not in a battleground), not on the world thread (the drain, the managers - already there) and not in the bypass scope.
// The key at 0: false before anything else is read.
bool WritePathRoutes(PlayerbotAI* ai);

// The producers' ops (the bot's own thread; ids only, the group read from the bot). true = queued; refused (queue full, an op
// of one of its bots pending): false, nothing changed. QueueLeave's tail (GroupOpPolicy.h LeaveTailOf): kLeaveNoTail for
// RepopAction, kLeaveBotMaster or 0 for LeaveGroupAction::Leave.
bool QueueLeave(PlayerbotAI* ai, OpSource source, std::uint8_t tail);
bool QueueAccept(PlayerbotAI* ai, std::uint32_t inviterGuidLow, bool decline);
bool QueueInvite(PlayerbotAI* ai, Player* invitee, bool raidConvert);
bool QueueLeader(PlayerbotAI* ai, Player* newLeader, bool reset);

// ---- A3 step 6 T30-0 (design T30 v1.1 3.5): the group manager's op path; the one caller is GroupManager.cpp (T30-1) ----------
// World thread only (the manager pass in RandomPlayerbotMgr::UpdateAIInternal, maps idle). They take guids, not pointers: the
// group manager reads no Player*; these look the bots up themselves (FindPlayer, GetBotAI) to set the pending marks Enqueue
// needs. source = OpSource::Manager. true = queued; false = a bot not found, a dungeon type (T34) or the push refused.
// Join: the inviter's group joins the invitee (groupId 0 = a new group; Join n+1 of a QUEST group carries its confirmed id, M8).
// RunJoin applies the size and range of the type (QUEST: QuestGroups.MaxSize / RecruitRadius, ADHOC: BotGroups.MaxBots /
// 2 x AdHoc.Radius) and keeps a manager group out of the ad-hoc registry (3.5 rows 1-4).
bool QueueManagerJoin(std::uint32_t inviteeLow, std::uint32_t inviterLow, std::uint32_t groupId, group_mgr::GroupType type,
    std::uint32_t questId, std::uint8_t objective);
// Leave of the manager's group (kLeaveQueued: the LeaveGroupAction tail; another group by now -> DropGroupChanged).
bool QueueManagerLeave(std::uint32_t botLow, std::uint32_t groupId, std::uint32_t leaderLow);
// World thread: totals since start, and the outcomes of manager ops since the last take (bounded, kManagerOutcomesMax).
ManagerOpCounts ReadManagerOpCounts();
std::size_t TakeManagerOutcomes(std::vector<ManagerOutcome>& out);
}
