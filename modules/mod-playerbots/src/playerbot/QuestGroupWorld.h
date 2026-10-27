#pragma once

#include "playerbot/QuestGroupPolicy.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
class Group;
class PlayerbotAI;

namespace ai
{
class TravelDestination;
class WorldPosition;

namespace quest_groups
{
// twow-repo#627 S7 (AiPlayerbot.QuestGroups.Enable): the world-thread manager of the help groups, run from the roster branch
// of RandomPlayerbotMgr::UpdateAIInternal next to CheckBgFill (end of World::Update, all maps idle, the #517 pattern).
// Group APIs (invite, accept, leave) and forced travel targets of the members happen only there; strategy changes and AI
// resets are posted to the bot and run on its own thread (twow-repo#628 fix, GroupAiRefresh.h).

// --- queries for the bots' own threads (CLI-Deep hooks: PosStuck, S3c, turn-in recovery, QFO, S5a) ------------------------
// Role of the bot in a help group: 0 = none, 1 = follower, 2 = leader. An atomic in PlayerbotAI, no lock.
bool IsMember(PlayerbotAI* ai);
bool IsFollower(PlayerbotAI* ai);
// PosStuck (CLI-Deep) calls this when it detects a leader without a forced target: the leader made no progress for its window,
// so the manager dissolves the group with reason=leader_stuck on its next pass; PosStuck itself then resets its window and
// leaves the leader alone (no park, unstuck or teleport). Lock-free, any thread.
void NoteLeaderStuck(PlayerbotAI* ai);
// Seconds the bot has waited for help on the quest / on its oldest waiting quest (0 = not waiting). Short mutex, rare calls.
std::uint32_t QueuedAge(std::uint32_t guid, std::uint32_t questId);
std::uint32_t OldestQueuedAge(std::uint32_t guid);
// twow-repo#365 HelpUntilDone (OB-10 review #660 B1), world thread only: the bot belongs to a help group of this manager, so
// its role is S7's (a member, or a bot in a StagedOps queue) - the ad-hoc manager leaves it alone when it drops a helper.
bool OwnsBot(std::uint32_t guid);
// twow-repo#628 fix, bot thread only (PlayerbotAI::ApplyGroupAiRequests): the follower or turn-in strategy set the manager posted.
void ApplyRoleSet(PlayerbotAI* ai, bool turnIn);
// The forced quest travel (objective objIdx 0..3, or the taker with -1). World thread with StagedOps 0; with StagedOps 1 the
// manager posts it and the bot runs it here on its own thread (ApplyGroupAiRequests, ForceTravel). False: no destination
// found or a search running (the manager forces again on a later pass).
bool ForceQuestTravel(Player* bot, std::uint32_t questId, int objIdx);

// A member's own travel target before the group, restored after it (no second TravelTarget: its constructor and SetTarget
// have side effects). Not kept: temporary, group-copy, gap-window and band-trip targets.
struct SavedTravel
{
    TravelDestination* dest = nullptr;
    WorldPosition* pos = nullptr;
    std::vector<std::string> conditions;
    bool forced = false;
    std::uint32_t relevance = 0;
    bool valid = false;
};

class WorldThreadManager
{
public:
    bool Owns(std::uint32_t guid) const { return memberOf.count(guid) != 0 || staged.count(guid) != 0; }
    void Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    // Running groups are tended (and dissolved) even after Enable went to 0 by a config reload.
    bool HasGroups() const { return !groups.empty() || !forming.empty() || !releasing.empty(); }

private:

    struct Member
    {
        std::uint32_t guid = 0;
        std::uint32_t lastCounter = 0;
        SavedTravel travel;
    };

    struct HelpGroup
    {
        std::uint32_t groupId = 0;
        std::uint32_t questId = 0;
        std::uint32_t leader = 0;
        std::uint32_t mapId = 0;
        std::vector<Member> members;
        Phase phase = Phase::Rendezvous;
        std::uint32_t formedAt = 0;
        std::uint32_t phaseSince = 0;
        std::uint32_t lastProgress = 0;
        std::uint32_t forces = 0;
        char cohort = 'A';
        bool mixed = false;
        ApproachWatch leaderWatch;   // the leader's approach to its forced target (PosStuck does not see forced targets)
    };

    struct Minute
    {
        std::uint32_t start = 0;
        std::uint32_t passes = 0;
        std::uint32_t candidates = 0;
        std::uint32_t quests = 0;
        std::uint32_t formed = 0;
        std::uint32_t dissolved = 0;
        std::uint32_t scanUsMax = 0;
        // twow-repo#628 fix: the slowest single formation and dissolution (the group change on the world thread).
        std::uint32_t formUsMax = 0;
        std::uint32_t dissolveUsMax = 0;
        std::uint32_t scanOnlyUsMax = 0;   // the quest-log index pass of all bots, without the formations
        std::uint32_t bots = 0;
        std::uint32_t slices = 0;   // updates of the last finished scan
        // twow-repo#627 S7 StagedOps sub-times (S7-A 25.10.: form_us_max 2.9-13.8 ms): the slowest invite opcode, accept
        // action, group setup (loot, update, roles) and member release, and the longest operation queue.
        std::uint32_t inviteUsMax = 0;
        std::uint32_t acceptUsMax = 0;
        std::uint32_t setupUsMax = 0;
        std::uint32_t releaseUsMax = 0;
        std::uint32_t queuedMax = 0;
        std::uint32_t ops = 0;
        // s7r 26.10.2026: the Tend loop of one update, the core opcode alone of a join / leave (GroupOpTiming.h), and the
        // slowest update's time off the CPU (ThreadCpuClock.h: preemption, blocking faults, lock waits, file I/O).
        std::uint32_t tendUsMax = 0;
        std::uint32_t acceptCoreUsMax = 0;
        std::uint32_t leaveCoreUsMax = 0;
        std::uint32_t stallUsMax = 0;
        std::uint32_t deferredDissolves = 0;   // OneHeavyStep: dissolutions carried to a later update
    };

    // twow-repo#627 S7 (AiPlayerbot.QuestGroups.StagedOps, default 0): S7-A 25.10. broke the 2 ms guard with a formation of
    // two bots (2.9-4.3 ms) and dissolutions (up to 4.5 ms) besides the scan. With 1 the manager does at most ONE group
    // operation per update - one member's invite and accept, the setup of a formed group, or one member's release - from two
    // queues (releases first), and the forced quest travel runs on the bot's own thread. Bots in a queue are no candidates.
    struct StagedForm
    {
        std::uint32_t questId = 0;
        std::vector<std::uint32_t> invitees;   // [0] = the leader
        std::size_t next = 1;                  // the next invitee
        std::vector<Member> joined;
        Member leader;
        std::uint32_t lo = 0;
        std::uint32_t hi = 0;
        bool mixed = false;
        std::uint32_t groupId = 0;             // the core group, once the first member joined
    };

    struct StagedRelease
    {
        std::uint32_t groupId = 0;
        Member member;
        bool leaveGroup = true;   // still in the group: leave it (the release itself decides again)
    };

    // twow-repo#628 (AiPlayerbot.QuestGroups.ScanSlices, default 0): one scan - the quest logs of a slice of the bots per
    // update, the formations once the last slice is done.
    struct ScanRound
    {
        bool active = false;
        std::vector<std::uint32_t> guids;   // the bots at the scan start, in map order
        std::size_t next = 0;
        std::uint32_t updates = 0;
        std::unordered_map<std::uint64_t, std::vector<Candidate>> buckets;
        std::unordered_map<std::uint64_t, bool> dropOnly;
    };

    void Tend(std::map<std::uint32_t, Player*> const& bots, HelpGroup& g, std::uint32_t now, Dissolve& out);
    void ScanBots(std::map<std::uint32_t, Player*> const& bots, std::size_t end, std::uint32_t now);
    void FormRound(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    bool FormOne(std::map<std::uint32_t, Player*> const& bots, std::vector<Candidate> const& members, std::uint32_t questId,
        std::uint32_t now);
    void DissolveGroup(std::map<std::uint32_t, Player*> const& bots, HelpGroup& g, Dissolve reason, std::uint32_t now);
    void Release(Player* bot, Member const& m, bool leaveGroup);
    void FlushMinute(std::uint32_t now);
    // StagedOps
    bool Staged() const;
    void StageForm(std::map<std::uint32_t, Player*> const& bots, std::vector<Candidate> const& members, std::uint32_t questId,
        std::uint32_t now);
    void StageRelease(std::uint32_t groupId, Member const& m);
    // True when it ran a group operation (OneHeavyStep: no scan slice after it in the same update).
    bool StepOne(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    void FinishForm(std::map<std::uint32_t, Player*> const& bots, StagedForm& f, std::uint32_t now);
    void Force(Player* bot, std::uint32_t questId, int objIdx, bool& forced);

    std::unordered_map<std::uint32_t, HelpGroup> groups;        // by core group id; world thread only
    std::unordered_map<std::uint32_t, std::uint32_t> memberOf;  // bot guid -> core group id
    std::uint32_t lastScan = 0;
    std::uint32_t passFormUs = 0;   // time in FormOne during the running scan
    ScanRound round;
    Minute minute;
    std::deque<StagedForm> forming;                 // StagedOps
    std::deque<StagedRelease> releasing;
    std::unordered_set<std::uint32_t> staged;      // bots in either queue
    // s7r (AiPlayerbot.QuestGroups.OneHeavyStep): groups Tend ended that wait for their own update - dissolved first in the
    // next one, not tended again (a reason like leader_stuck is taken once and would be lost).
    std::vector<std::pair<std::uint32_t, Dissolve>> dissolveLater;
};

WorldThreadManager& WorldManager();
QueueStore& Queue();
}
}
